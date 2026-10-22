#include "Aim.hpp"
#include <bedrocktools/Api.hpp>
#include <bedrocktools/events/RuntimeListener.hpp>
#include <bedrocktools/sdk/Functions.hpp>
#include <bedrocktools/sdk/world/Actor.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <chrono>
#include <mutex>
#include <vector>
#include <charconv>

namespace {
namespace bt = bedrocktools;
namespace sdk = bt::sdk;
using Clock = std::chrono::steady_clock;
constexpr auto moduleId = "adaptiveaim.assist";
struct Settings {
    bool enabled=false, adaptive=true, head=false;
    float fov=8, range=4, response=5, speed=35;
};
class AdaptiveAim {
    std::mutex mutex;
    Settings settings;
    bool chat=false, container=false;
    Clock::time_point last{};
    bt::events::RuntimeListener<bt::events::LocalPlayerTickEvent> ticks;
    bt::events::RuntimeListener<bt::events::ScreenStateEvent> screens;
    using List = std::vector<void*>(*)(void*);
    using Check = bool(*)(void*);
    using Solid = bool(*)(void*, const sdk::BlockPos&);
    using Turn = void(*)(void*, sdk::Vec2*);
    List list=nullptr;
    Check isPlayer=nullptr, invisible=nullptr;
    Solid solid=nullptr;
    Turn turn=nullptr;

    // Conservative voxel traversal: any solid voxel intersected by the segment blocks it.
    // Not a shape-aware Minecraft raycast (glass/partial blocks can differ).
    bool visible(sdk::BlockSource* source, aim::Vec3 a, aim::Vec3 b) {
        if (!source || !aim::finite(a) || !aim::finite(b)) return false;
        int cell[3]={(int)std::floor(a.x),(int)std::floor(a.y),(int)std::floor(a.z)};
        int end[3]={(int)std::floor(b.x),(int)std::floor(b.y),(int)std::floor(b.z)};
        float origin[3]={a.x,a.y,a.z}, d[3]={b.x-a.x,b.y-a.y,b.z-a.z};
        int sign[3]; float next[3], stride[3];
        for(int i=0;i<3;++i) {
            sign[i]=d[i]>0?1:-1;
            stride[i]=d[i]==0?INFINITY:std::abs(1/d[i]);
            next[i]=d[i]==0?INFINITY:((cell[i]+(sign[i]>0?1:0))-origin[i])/d[i];
        }
        for(int n=0;n<128;++n) {
            if(solid(source,{cell[0],cell[1],cell[2]})) return false;
            if(cell[0]==end[0] && cell[1]==end[1] && cell[2]==end[2]) return true;
            int axis=next[0]<next[1]?0:1;
            if(next[2]<next[axis]) axis=2;
            cell[axis]+=sign[axis]; next[axis]+=stride[axis];
        }
        return false;
    }
    void tick(bt::events::LocalPlayerTickEvent& event) {
        std::lock_guard lock(mutex);
        const auto now=Clock::now();
        float dt=last==Clock::time_point{}?0:std::chrono::duration<float>(now-last).count();
        last=now;
        auto* player=event.player;
        if(!settings.enabled || chat || container || !player || dt<=0 || dt>.2f) return;
        auto* level=player->level(); auto* dimension=player->dimension();
        if(!level || !dimension || !level->actorManager() || !player->rotationComponent()) return;
        const auto box=player->bounds();
        aim::Vec3 eye{(box.min.x+box.max.x)*.5f,box.max.y-.12f,(box.min.z+box.max.z)*.5f};
        if(!aim::finite(eye)) return;
        auto rot=player->rotation();
        aim::Angles best{}; float score=settings.fov;
        bool found=false;
        // No raw actor pointers retained across ticks/world changes.
        for(void* raw:list(level->actorManager())) {
            if(!raw || raw==player || !isPlayer(raw) || invisible(raw)) continue;
            auto* actor=static_cast<sdk::Actor*>(raw);
            if(actor->dimension()!=dimension) continue;
            auto bounds=actor->bounds();
            float height=bounds.max.y-bounds.min.y;
            if(!std::isfinite(height) || height<.1f || height>4) continue;
            aim::Vec3 point{(bounds.min.x+bounds.max.x)*.5f,
                bounds.min.y+height*(settings.head?.9f:.55f),(bounds.min.z+bounds.max.z)*.5f};
            float distance=std::hypot(std::hypot(point.x-eye.x,point.z-eye.z),point.y-eye.y);
            if(!std::isfinite(distance) || distance>settings.range) continue;
            auto target=aim::towards(eye,point);
            if(!target) continue;
            auto error=aim::error({rot.x,rot.y},*target);
            float angle=aim::magnitude(error);
            if(angle>=score || !visible(dimension->blockSource(),eye,point)) continue;
            score=angle; best=error; found=true;
        }
        if(!found) return;
        auto delta=aim::step(best,dt,settings.response,settings.speed,settings.adaptive);
        delta.pitch=std::clamp(rot.x+delta.pitch,-89.9f,89.9f)-rot.x;
        sdk::Vec2 input{delta.pitch,delta.yaw};
        turn(player,&input);
    }
    void config(std::string_view key,std::string_view value) {
        std::lock_guard lock(mutex);
        if(key=="adaptive" || key=="head") {
            if(value!="true" && value!="false") return;
            (key=="adaptive"?settings.adaptive:settings.head)=value=="true";
            return;
        }
        float number=0;
        auto result=std::from_chars(value.data(),value.data()+value.size(),number);
        if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || !std::isfinite(number)) return;
        if(key=="fov") settings.fov=std::clamp(number,1.f,20.f);
        if(key=="range") settings.range=std::clamp(number,1.f,6.f);
        if(key=="response") settings.response=std::clamp(number,1.f,12.f);
        if(key=="speed") settings.speed=std::clamp(number,5.f,90.f);
    }
public:
    static AdaptiveAim& instance() { static AdaptiveAim mod; return mod; }
    bool load(pl::mod::ModContext&) { return true; }
    bool enable(pl::mod::ModContext& context) {
        auto* api=bt::api::find();
        if(!api) { context.logger().error("Enable the matching BedrockTools runtime first, then re-enable Adaptive Aim."); return false; }
        using Id=bt::memory::SignatureId;
        list=sdk::function<List>(Id::ActorManagerList,api);
        isPlayer=sdk::function<Check>(Id::ActorIsPlayer,api);
        invisible=sdk::function<Check>(Id::ActorIsInvisible,api);
        solid=sdk::function<Solid>(Id::BlockSourceIsSolidBlockingBlock,api);
        turn=sdk::function<Turn>(Id::LocalPlayerApplyTurnDelta,api);
        if(!list || !isPlayer || !invisible || !solid || !turn) return false;
        if(!screens.subscribe([this](auto& e) {
            std::lock_guard lock(mutex);
            (e.screen==bt::events::ScreenKind::Chat?chat:container)=e.phase==bt::events::ScreenPhase::Opened;
        },bt::events::EventPriority::Normal,api)) return false;
        if(!ticks.subscribe([this](auto& e){tick(e);},bt::events::EventPriority::Normal,api)) { screens.reset(); return false; }
        using namespace pl::modmenu;
        bool registered=ModuleBuilder(moduleId,"Adaptive Aim")
            .modId(context.id()).description("Smooth head/torso aim assistance. No auto-attack; no detection guarantees.")
            .defaultEnabled(false).hideInHudEditor()
            .onToggle([this](auto,bool enabled){std::lock_guard lock(mutex); settings.enabled=enabled; last={};})
            .onConfigChanged([this](auto,auto key,auto value){config(key,value);})
            .config("head","Aim at head (off = torso)",ConfigType::Toggle,"false")
            .config("adaptive","Adaptive slowdown",ConfigType::Toggle,"true")
            .config("fov","Aim cone (degrees)",ConfigType::SliderFloat,"8","1","20")
            .config("range","Range (blocks)",ConfigType::SliderFloat,"4","1","6")
            .config("response","Response",ConfigType::SliderFloat,"5","1","12")
            .config("speed","Maximum turn (degrees/sec)",ConfigType::SliderFloat,"35","5","90")
            .registerModule();
        if(!registered) { ticks.reset(); screens.reset(); }
        return registered;
    }
    bool disable(pl::mod::ModContext&) {
        ticks.reset(); screens.reset();
        pl::modmenu::unregisterModule(moduleId);
        std::lock_guard lock(mutex);
        settings=Settings{}; last={}; chat=container=false;
        return true;
    }
    bool unload(pl::mod::ModContext& context) { return disable(context); }
};
}
PL_REGISTER_MOD(AdaptiveAim, AdaptiveAim::instance())
