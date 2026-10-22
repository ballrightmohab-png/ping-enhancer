#include "Aim.hpp"
#include "Settings.hpp"
#include "CameraSmoother.hpp"
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
    // Identity tokens are compared only against the freshly enumerated actor list.
    // No cached target pointer is dereferenced on a later tick.
    std::uintptr_t targetToken=0, worldToken=0;
    bool awaitingRotation=false;
    aim::Angles rotationBaseline{};
    Clock::time_point firstRequest{};
    void resetTracking() {
        targetToken=0; awaitingRotation=false; firstRequest={}; cameraSmoother.reset();
    }

    void report(const std::string& text) {
        pl::modmenu::DrawCommand command{};
        command.type=pl::modmenu::DrawCommandType::Text;
        command.x=16; command.y=90; command.size=16;
        command.color=0xFFFFFFFF;
        command.text="Aim: "+text;
        pl::modmenu::submitDrawCommands(moduleId, std::span(&command,1));
    }
    void startMonitor() {
        stopping=false;
        monitor=std::thread([this] {
            while(!stopping.load()) {
                attachRuntime();
                std::string message;
                { std::lock_guard lock(mutex);
                    message=runtimeStatus;
                    if(attached) {
                        if(!settings.enabled) message="OFF - enable Adaptive Aim";
                        else if(lastTick==Clock::time_point{} || Clock::now()-lastTick>std::chrono::seconds(3))
                            message="Hooks installed, but NO world ticks (check game version)";
                        else message=tickStatus;
                    }
                }
                report(message);
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
        if(settings.strength<=0) { tickStatus="Paused: strength is zero"; resetTracking(); return; }
        if(!settings.players && !settings.mobs) { tickStatus="Paused: both target switches are off"; resetTracking(); return; }
        if(uiBlocked) { tickStatus="Paused: menu open or menu state not observed"; resetTracking(); return; }
        if(!player || dt<=0 || dt>.2f) { tickStatus="Waiting for regular player ticks"; resetTracking(); return; }
        auto* level=player->level(); auto* dimension=player->dimension();
        if(!level || !dimension || !level->actorManager() || !player->rotationComponent()) {
            tickStatus="Invalid world/player components - check game version"; resetTracking(); return;
        }
        if(worldToken!=reinterpret_cast<std::uintptr_t>(level)) {
            resetTracking(); worldToken=reinterpret_cast<std::uintptr_t>(level);
        }
        const auto box=player->bounds();
        aim::Vec3 eye{(box.min.x+box.max.x)*.5f,box.max.y-.12f,(box.min.z+box.max.z)*.5f};
        if(!aim::finite(eye)) { tickStatus="Invalid player bounds"; resetTracking(); return; }
        auto rot=player->rotation();
        if(!std::isfinite(rot.x) || !std::isfinite(rot.y)) {
            tickStatus="Invalid player rotation"; resetTracking(); return;
        }
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
        // Subtract unreleased correction so a closed-loop tracker does not keep
        // accumulating the same camera error into the smoothing reservoir.
        const auto pending=settings.cameraSmoothing?cameraSmoother.pending():aim::Angles{};
        aim::Angles remaining{best.pitch-pending.pitch,best.yaw-pending.yaw};
        auto delta=aim::step(remaining,dt,10.f,settings.speed,settings.adaptive);
        const float strength=settings.strength/100.f;
        delta.pitch*=strength;
        delta.yaw*=strength;
        if(settings.cameraSmoothing)
            delta=cameraSmoother.advance(delta,dt,settings.smoothness,settings.speed*strength);
        delta.pitch=std::clamp(rot.x+delta.pitch,-89.9f,89.9f)-rot.x;
        if(aim::magnitude(best)<.05f) {
            tickStatus="Auto tracking: aligned (no input needed)";
            awaitingRotation=false; cameraSmoother.reset();
            return;
        }
        if(!awaitingRotation) {
            rotationBaseline={rot.x,rot.y}; firstRequest=now; awaitingRotation=true;
        }
        const auto mapped=aim::toLookDelta(delta,settings.yawFirst);
        sdk::Vec2 input{mapped.first,mapped.second};
        if(!runtime.sendAutomaticTurn(player,input)) {
            tickStatus="Automatic camera callback unavailable"; resetTracking(); return;
        }
        if(rotationChanged) tickStatus="Auto tracking: player rotation updated";
        else if(now-firstRequest>std::chrono::milliseconds(500))
            tickStatus="Auto requests sent; camera response unconfirmed (check input axes)";
        else tickStatus="Auto tracking: waiting for later camera update";
    }
    void config(std::string_view key,std::string_view value) {
        std::lock_guard lock(mutex);
        if(settings.update(key,value)) resetTracking();
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
                { std::lock_guard lock(mutex); settings.enabled=enabled; last={}; resetTracking(); }
                // Retry when toggled on; late game startup is handled by the monitor.
                if(enabled) attachRuntime();
            })
            .onConfigChanged([this](auto,auto key,auto value){config(key,value);})
            .config("players","Target players",ConfigType::Toggle,"true")
            .config("mobs","Target mobs",ConfigType::Toggle,"false")
            .config("strength","Aim strength (%)",ConfigType::SliderFloat,"50","0","100")
            .config("head","Aim at head (off = torso)",ConfigType::Toggle,"false")
            .config("adaptive","Adaptive slowdown",ConfigType::Toggle,"true")
            .config("fov","Aim cone (degrees)",ConfigType::SliderFloat,"60","1","180")
            .config("range","Range (blocks)",ConfigType::SliderFloat,"4","1","6")
            .config("cameraSmoothing","Frame-gen camera smoothing",ConfigType::Toggle,"true")
            .config("smoothness","Camera smoothness",ConfigType::SliderFloat,"50","0","95")
            .config("yawFirst","Camera input: yaw first (advanced)",ConfigType::Toggle,"false")
            .config("speed","Maximum turn (degrees/sec)",ConfigType::SliderFloat,"70","5","90")
            .registerModule();
        if(!registered) return false;
        if(!attachRuntime()) {
            context.logger().warn("Adaptive Aim controls are available; Minecraft signatures/hooks are not ready. No other mod is required. See the live status.");
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
        settings=aim::Settings{}; last={}; lastTick={}; attached=false; uiBlocked=false; lastResolve={}; worldToken=0; resetTracking();
        return true;
    }
    bool unload(pl::mod::ModContext& context) { return disable(context); }
};
}
PL_REGISTER_MOD(AdaptiveAim, AdaptiveAim::instance())
