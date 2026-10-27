#include "Aim.hpp"
#include "Settings.hpp"
#include "CameraSmoother.hpp"
#include "LookCalibration.hpp"
#include "Strength.hpp"
#include "game/Runtime.hpp"
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <chrono>
#include <mutex>
#include <vector>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace {
namespace sdk = game;
using Clock = std::chrono::steady_clock;
constexpr auto moduleId = "adaptiveaim.assist";
class AdaptiveAim {
    std::mutex mutex;
    aim::Settings settings;
    aim::CameraSmoother cameraSmoother;
    aim::LookCalibration calibration;
    std::uint64_t lastManualSequence=0;
    std::mutex runtimeMutex;
    bool uiBlocked=false;
    game::Runtime runtime;
    Clock::time_point lastResolve{};
    std::thread monitor;
    std::mutex monitorMutex;
    std::condition_variable wakeMonitor;
    std::atomic<bool> stopping{false};
    std::string runtimeStatus="Waiting for Minecraft game hooks";
    std::string tickStatus="Waiting for world ticks";
    Clock::time_point lastTick{};
    bool attached=false;
    bool trackingFault=false;
    int feedbackSource=0; // 1: game pick ray; 2: first-person actor rotation.
    // Identity tokens are compared only against the freshly enumerated actor list.
    // No cached target pointer is dereferenced on a later tick.
    std::uintptr_t targetToken=0, worldToken=0;
    bool awaitingRotation=false;
    aim::Angles rotationBaseline{};
    Clock::time_point firstRequest{};
    void resetTracking() {
        targetToken=0; awaitingRotation=false; firstRequest={}; cameraSmoother.reset(); calibration.cancelPending();
    }

    void startMonitor() {
        stopping=false;
        monitor=std::thread([this] {
            while(!stopping.load()) {
                attachRuntime();
                std::unique_lock lock(monitorMutex);
                wakeMonitor.wait_for(lock,std::chrono::milliseconds(500),[this]{return stopping.load();});
            }
        });
    }
    void stopMonitor() {
        stopping=true; wakeMonitor.notify_all();
        if(monitor.joinable()) monitor.join();
    }
    Clock::time_point last{};
    using List = std::vector<void*>(*)(void*);
    using Check = bool(*)(void*);
    using Solid = bool(*)(void*, const sdk::BlockPos&);
    List list=nullptr;
    Check isPlayer=nullptr, invisible=nullptr;
    Solid solid=nullptr;

    // Conservative voxel traversal: any solid voxel intersected by the segment blocks it.
    // Not a shape-aware Minecraft raycast (glass/partial blocks can differ).
    bool visible(sdk::BlockSource* source, aim::Vec3 a, aim::Vec3 b) {
        return source && aim::clearVoxels(a,b,[&](int x,int y,int z) {
            return solid(source,{x,y,z});
        });
    }
    void tick(sdk::Player* player) {
        std::lock_guard lock(mutex);
        const auto now=Clock::now();
        float dt=last==Clock::time_point{}?0:std::chrono::duration<float>(now-last).count();
        last=now;
        lastTick=now;
        // This path runs on game ticks, never gated on touch/mouse activity.
        if(!settings.enabled) { tickStatus="OFF - enable Auto Target Tracking"; resetTracking(); return; }
        if(trackingFault) { tickStatus="Tracking paused: view did not respond; toggle off/on"; return; }
        if(settings.strength<=0) { tickStatus="Paused: strength is zero"; resetTracking(); return; }
        if(!settings.players && !settings.mobs) { tickStatus="Paused: both target switches are off"; resetTracking(); return; }
        if(uiBlocked) { tickStatus="Paused: menu open or menu state not observed"; resetTracking(); return; }
        if(!player || dt<=0 || dt>.2f) { tickStatus="Waiting for regular player ticks"; resetTracking(); return; }
        auto* level=player->level(); auto* dimension=player->dimension();
        if(!level || !dimension || !level->actorManager() || !player->rotationComponent()) {
            tickStatus="Invalid world/player components - check game version"; resetTracking(); return;
        }
        if(worldToken!=reinterpret_cast<std::uintptr_t>(level)) {
            resetTracking(); calibration.reset(); feedbackSource=0; worldToken=reinterpret_cast<std::uintptr_t>(level);
        }
        const auto box=player->bounds();
        const auto ray=level->viewRay();
        aim::Vec3 eye{ray.origin.x,ray.origin.y,ray.origin.z};
        const aim::Vec3 direction{ray.direction.x,ray.direction.y,ray.direction.z};
        const aim::Vec3 bodyEye{(box.min.x+box.max.x)*.5f,box.max.y-.12f,(box.min.z+box.max.z)*.5f};
        const float rayLength=std::hypot(std::hypot(direction.x,direction.z),direction.y);
        const float originDistance=std::hypot(std::hypot(eye.x-bodyEye.x,eye.z-bodyEye.z),eye.y-bodyEye.y);
        const bool validRay=aim::finite(eye) && aim::finite(direction) && aim::finite(bodyEye) &&
            std::isfinite(rayLength) && rayLength>=.01f && rayLength<=512.f &&
            std::isfinite(originDistance) && originDistance<=3.f;
        std::optional<aim::Angles> view;
        int source=1;
        // Keep the fallback until a reset, rather than mixing two feedback
        // frames when an intermittently populated pick result appears/disappears.
        if(validRay && feedbackSource!=2) view=aim::towards({0,0,0},direction);
        if(!view) {
            // Some Android layouts/control paths do not expose a usable pick ray.
            // In first person, use the existing actor rotation component instead;
            // measured turn response is still mandatory by default. Never invent
            // a direction from zero/invalid ray bytes or reinterpret cursor data.
            const auto rotation=player->rotation();
            if(!aim::finite(bodyEye) || !std::isfinite(rotation.x) ||
               !std::isfinite(rotation.y) || std::abs(rotation.x)>90.f) {
                tickStatus="No usable view ray or first-person rotation"; resetTracking(); return;
            }
            eye=bodyEye;
            view=aim::Angles{rotation.x,std::remainder(rotation.y,360.f)};
            source=2;
        }
        if(source!=feedbackSource) {
            resetTracking(); calibration.reset(); feedbackSource=source;
        }
        sdk::Vec2 rot{view->pitch,view->yaw};
        bool rotationChanged=awaitingRotation && aim::magnitude(aim::error(rotationBaseline,{rot.x,rot.y}))>.01f;
        if(rotationChanged) { rotationBaseline={rot.x,rot.y}; firstRequest=now; }
        aim::Angles best{}; float score=INFINITY;
        std::uintptr_t bestToken=0;
        bool found=false;
        int candidates=0, inRange=0, inCone=0, blocked=0;
        // No raw actor pointers retained across ticks/world changes.
        for(void* raw:list(level->actorManager())) {
            if(!raw || raw==player || invisible(raw)) continue;
            auto* actor=static_cast<sdk::Actor*>(raw);
            // Same Mob category bit (2) used by BedrockTools Hitbox.
            if(!settings.accepts(isPlayer(raw), actor->hasCategory(2))) continue;
            ++candidates;
            if(actor->dimension()!=dimension) continue;
            auto bounds=actor->bounds();
            float height=bounds.max.y-bounds.min.y;
            if(!std::isfinite(height) || height<.1f || height>4) continue;
            aim::Vec3 point{(bounds.min.x+bounds.max.x)*.5f,
                bounds.min.y+height*(settings.head?.9f:.55f),(bounds.min.z+bounds.max.z)*.5f};
            float distance=std::hypot(std::hypot(point.x-eye.x,point.z-eye.z),point.y-eye.y);
            if(!std::isfinite(distance) || distance>settings.range) continue;
            ++inRange;
            auto target=aim::towards(eye,point);
            if(!target) continue;
            auto error=aim::error({rot.x,rot.y},*target);
            float angle=aim::magnitude(error);
            if(angle>=settings.fov) continue;
            ++inCone;
            const auto token=reinterpret_cast<std::uintptr_t>(raw);
            // Prefer a still-valid tracked entity over similarly aligned neighbours.
            const float selectionScore=angle+(targetToken && token!=targetToken?2.f:0.f);
            if(selectionScore>=score) continue;
            if(!visible(dimension->blockSource(),eye,point)) { ++blocked; continue; }
            score=selectionScore; best=error; bestToken=token; found=true;
        }
        if(!found) {
            if(!candidates) tickStatus="No enabled targets (turn Target mobs ON for chickens)";
            else if(!inRange) tickStatus="Targets outside range, or invalid bounds";
            else if(!inCone) tickStatus="Outside aim cone - aim closer or increase cone";
            else if(blocked) tickStatus="Target blocked by solid voxels";
            else tickStatus="No valid target";
            resetTracking();
            return;
        }
        if(bestToken!=targetToken) { awaitingRotation=false; firstRequest={}; cameraSmoother.reset(); }
        targetToken=bestToken;
        const auto manualSequence=runtime.physicalLookSequence();
        const bool manualLook=manualSequence!=lastManualSequence;
        lastManualSequence=manualSequence;
        if(settings.autoCalibrate && !calibration.ready()) {
            cameraSmoother.reset();
            const auto probe=calibration.update(*view,std::chrono::duration<double>(now.time_since_epoch()).count(),manualLook);
            if(probe) runtime.sendAutomaticTurn(player,{probe->first,probe->second});
            tickStatus=calibration.failed()?"Calibration failed - toggle off/on; keep camera still briefly":
                "Calibrating view axes - keep camera still briefly";
            return;
        }
        auto delta=aim::strengthCorrection(best,dt,settings.strength,settings.speed,
            settings.adaptive,settings.cameraSmoothing,settings.smoothness,cameraSmoother);
        delta.pitch=std::clamp(rot.x+delta.pitch,-89.9f,89.9f)-rot.x;
        if(aim::magnitude(best)<.05f) {
            tickStatus="Auto tracking: aligned (no input needed)";
            awaitingRotation=false; cameraSmoother.reset();
            return;
        }
        // Strongest lock must not stack full corrections against stale feedback.
        // Wait for the previous command to produce a view update before another.
        if(settings.strength>=100.f && awaitingRotation && !rotationChanged) {
            if(now-firstRequest>std::chrono::milliseconds(500)) {
                trackingFault=true; resetTracking();
                tickStatus="Tracking paused: view did not respond; toggle off/on";
            }
            return;
        }
        if(!awaitingRotation) {
            rotationBaseline={rot.x,rot.y}; firstRequest=now; awaitingRotation=true;
        }
        const auto mapped=settings.autoCalibrate?calibration.encode(delta):
            std::optional<aim::LookDelta>{aim::toLookDelta(delta,settings.yawFirst)};
        if(!mapped) { tickStatus="Camera response mapping unavailable"; resetTracking(); return; }
        sdk::Vec2 input{mapped->first,mapped->second};
        if(!runtime.sendAutomaticTurn(player,input)) {
            tickStatus="Automatic camera callback unavailable"; resetTracking(); return;
        }
        if(rotationChanged) tickStatus="Crosshair tracking: view ray updated";
        else if(now-firstRequest>std::chrono::milliseconds(500)) {
            tickStatus="Tracking paused: view did not respond; toggle off/on";
            trackingFault=true; resetTracking();
        }
        else tickStatus="Auto tracking: waiting for later camera update";
    }
    void config(std::string_view key,std::string_view value) {
        std::lock_guard lock(mutex);
        if(settings.update(key,value)) { resetTracking(); calibration.reset(); trackingFault=false; feedbackSource=0; }
    }
    bool attachRuntime() {
        std::lock_guard runtimeLock(runtimeMutex);
        { std::lock_guard lock(mutex); if(attached) return true; }
        // A mismatched game must not be rescanned continuously.
        const auto now=Clock::now();
        if(lastResolve!=Clock::time_point{} && now-lastResolve<std::chrono::seconds(2)) return false;
        lastResolve=now;
        std::string status;
        if(!runtime.resolve(status)) {
            std::lock_guard lock(mutex); runtimeStatus=status; return false;
        }
        using Id=game::Signature;
        list=runtime.function<List>(Id::ActorManagerList);
        isPlayer=runtime.function<Check>(Id::ActorIsPlayer);
        invisible=runtime.function<Check>(Id::ActorIsInvisible);
        solid=runtime.function<Solid>(Id::BlockSourceIsSolidBlockingBlock);
        bool installed=runtime.install([this](auto* player){tick(player);},[this](bool blocked){
            std::lock_guard lock(mutex); uiBlocked=blocked;
            if(blocked) { resetTracking(); calibration.reset(); feedbackSource=0; }
        },status);
        { std::lock_guard lock(mutex); attached=installed; runtimeStatus=status; }
        return installed;
    }
public:
    ~AdaptiveAim() { stopMonitor(); runtime.stop(); }
    static AdaptiveAim& instance() { static AdaptiveAim mod; return mod; }
    bool load(pl::mod::ModContext&) { return true; }
    bool enable(pl::mod::ModContext& context) {
        // Register controls before scanning/hooking Minecraft.
        // Unsupported game versions must not make the controls disappear.
        using namespace pl::modmenu;
        bool registered=ModuleBuilder(moduleId,"Auto Target Tracking")
            .modId(context.id()).description("Automatic head/torso tracking, even without mouse or touch movement. No auto-attack.")
            .defaultEnabled(false).hideInHudEditor()
            .onToggle([this](auto,bool enabled){
                { std::lock_guard lock(mutex); settings.enabled=enabled; last={}; resetTracking(); calibration.reset(); trackingFault=false; feedbackSource=0;
                  lastManualSequence=runtime.physicalLookSequence(); }
                // Retry when toggled on; late game startup is handled by the monitor.
                if(enabled) attachRuntime();
            })
            .onConfigChanged([this](auto,auto key,auto value){config(key,value);})
            .config("players","Target players",ConfigType::Toggle,"true")
            .config("mobs","Target mobs",ConfigType::Toggle,"false")
            .config("strength","Aim strength (100 = AIM BOT!)",ConfigType::SliderInt,"50","10","100")
            .config("head","Aim at head (off = torso)",ConfigType::Toggle,"false")
            .config("adaptive","Adaptive slowdown",ConfigType::Toggle,"true")
            .config("fov","Aim cone (degrees)",ConfigType::SliderFloat,"60","1","180")
            .config("range","Range (blocks)",ConfigType::SliderFloat,"4","1","6")
            .config("autoCalibrate","Auto-calibrate camera axes",ConfigType::Toggle,"true")
            .config("cameraSmoothing","Frame-gen camera smoothing",ConfigType::Toggle,"true")
            .config("smoothness","Camera smoothness",ConfigType::SliderFloat,"50","0","95")
            .config("yawFirst","Camera input: yaw first (advanced)",ConfigType::Toggle,"false")
            .config("speed","Base turn speed (below 100 strength)",ConfigType::SliderFloat,"70","5","90")
            .registerModule();
        if(!registered) return false;
        if(!attachRuntime()) {
            context.logger().warn("Adaptive Aim controls are available; Minecraft signatures/hooks are not ready. No other mod is required.");
        }
        startMonitor();
        return true;
    }
    bool disable(pl::mod::ModContext&) {
        stopMonitor();
        std::lock_guard runtimeLock(runtimeMutex);
        runtime.stop();
        pl::modmenu::unregisterModule(moduleId);
        std::lock_guard lock(mutex);
        settings=aim::Settings{}; last={}; lastTick={}; attached=false; uiBlocked=false; lastResolve={}; worldToken=0; resetTracking(); calibration.reset(); trackingFault=false; feedbackSource=0;
        return true;
    }
    bool unload(pl::mod::ModContext& context) { return disable(context); }
};
}
PL_REGISTER_MOD(AdaptiveAim, AdaptiveAim::instance())
