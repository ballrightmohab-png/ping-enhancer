#include <pl/ModMenu.hpp>
#include <cassert>
#include <optional>
#include <mutex>
#include <array>
#include <thread>
#include <cstring>
#include "FakeHost.hpp"
namespace {
std::optional<pl::modmenu::ModuleInfo> registeredMenu;
std::string lastStatus;
std::mutex statusMutex;
}
namespace pl::modmenu {
bool registerModule(const ModuleInfo& info) { registeredMenu=info; return true; }
void unregisterModule(std::string_view) { registeredMenu.reset(); }
void submitDrawCommands(std::string_view,std::span<const DrawCommand> commands) {
    std::lock_guard lock(statusMutex);
    if(!commands.empty()) lastStatus=commands[0].text;
}
}
#include "../src/Mod.cpp"
struct ActorFixture {
    alignas(16) std::array<unsigned char,4096> storage{};
    sdk::Vec3 position{};
    sdk::Vec2 rotation{};
    sdk::AABB bounds{};
    template<class T> void put(std::size_t offset, T value) { std::memcpy(storage.data()+offset,&value,sizeof(value)); }
    ActorFixture(sdk::AABB box,void* dimension,void* level,bool mob) : bounds(box) {
        put(sdk::offsets::Actor::mStateVectorComponent,&position);
        put(sdk::offsets::Actor::mStateVectorComponent+sdk::offsets::BuiltInActorComponents::mAABBShapeComponent,&bounds);
        put(sdk::offsets::Actor::mActorRotationComponent,&rotation);
        put(sdk::offsets::Actor::mDimension,dimension);
        put(sdk::offsets::Actor::mLevel,level);
        put(sdk::offsets::Actor::mCategories,std::uint32_t(mob?2:0));
    }
    void* ptr() { return storage.data(); }
};
int main() {
    pl::mod::ModContext context;
    auto& mod=AdaptiveAim::instance();
    assert(mod.enable(context));
    auto config=registeredMenu->onConfigChanged;
    auto toggle=registeredMenu->onToggle;
    config(moduleId,"autoCalibrate","false");
    config(moduleId,"players","false");
    config(moduleId,"mobs","true");
    config(moduleId,"strength","100");
    config(moduleId,"fov","60");
    toggle(moduleId,true); // Minecraft deliberately not available yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::lock_guard lock(statusMutex); assert(lastStatus.find("Missing game functions")!=std::string::npos); }
    fakehost::gameReady=true;
    for(int n=0;n<200 && fakehost::hookCount()!=3;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(fakehost::hookCount()==3); // Automatic game hook installation.
    alignas(16) std::array<unsigned char,4096> dimension{}, level{};
    int region=0, manager=0;
    auto* regionPtr=&region; auto* managerPtr=&manager;
    std::memcpy(dimension.data()+sdk::offsets::Dimension::mBlockSource,&regionPtr,sizeof(regionPtr));
    std::memcpy(level.data()+sdk::offsets::Level::mActorManager,&managerPtr,sizeof(managerPtr));
    ActorFixture local({{-.3f,0,-.3f},{.3f,1.8f,.3f}},dimension.data(),level.data(),true);
    // Chicken-sized target, 3 blocks forward; looking horizontally must turn down.
    ActorFixture chicken({{-.2f,0,2.8f},{.2f,.7f,3.2f}},dimension.data(),level.data(),true);
    ActorFixture otherPlayer({{9.7f,0,9.7f},{10.3f,1.8f,10.3f}},dimension.data(),level.data(),true);
    void* actors[]={chicken.ptr(),otherPlayer.ptr()};
    fakehost::actors.assign(actors,actors+2);
    fakehost::publishView=true;
    auto tick=[&]{std::this_thread::sleep_for(std::chrono::milliseconds(10)); fakehost::tick(local.ptr());};
    tick(); tick();
    assert(fakehost::turns==0); // Unknown menu state must pause aim.
    assert(!fakehost::queryMenu(false));
    tick(); tick();
    assert(fakehost::turns>0);
    assert(fakehost::lastDelta.x>0); // frame-gen pitch-first command.
    int before=fakehost::turns;
    assert(fakehost::queryMenu(true));
    tick(); assert(fakehost::turns==before);
    assert(!fakehost::queryMenu(false));
    tick(); assert(fakehost::turns>before);
    before=fakehost::turns;
    config(moduleId,"mobs","false"); tick(); assert(fakehost::turns==before);
    config(moduleId,"mobs","true");
    config(moduleId,"strength","0"); tick(); assert(fakehost::turns==before);
    config(moduleId,"strength","100");
    fakehost::blocked=true; tick(); assert(fakehost::turns==before);
    fakehost::blocked=false;
    config(moduleId,"fov","1"); tick(); assert(fakehost::turns==before);
    config(moduleId,"fov","60"); tick(); assert(fakehost::turns>before);
    before=fakehost::turns;
    // Treat the nearby fixture as a player: mobs ON must not override players OFF.
    void* swapped[]={otherPlayer.ptr(),chicken.ptr()}; fakehost::actors.assign(swapped,swapped+2);
    tick(); assert(fakehost::turns==before);
    config(moduleId,"players","true"); tick(); assert(fakehost::turns>before);

    // Automatic convergence with a DEFERRED CAMERA MODEL and zero look events.
    // This checks our closed loop under that declared model, NOT Android camera ABI.
    config(moduleId,"players","false");
    fakehost::actors.assign(actors,actors+2);
    config(moduleId,"fov","180");
    config(moduleId,"speed","90");
    config(moduleId,"strength","100");
    config(moduleId,"adaptive","false");
    chicken.bounds={{.8f,0,2.8f},{1.2f,.7f,3.2f}};
    local.rotation={}; fakehost::cameraPose={};
    fakehost::pendingCamera={}; fakehost::integrateCamera=true;
    auto desired=aim::towards({0,1.68f,0},{1,.385f,3});
    assert(desired);
    float initialError=aim::magnitude(aim::error({0,0},*desired));
    for(int i=0;i<70;++i) tick(); // No mouse/touch callbacks anywhere in this test.
    float finalError=aim::magnitude(aim::error({local.rotation.x,local.rotation.y},*desired));
    assert(finalError<initialError*.3f);
    assert(local.rotation.x>0 && local.rotation.y<0); // Both axes converge.
    // A moving target is re-acquired/re-aimed without any look input.
    chicken.bounds.min.x-=2; chicken.bounds.max.x-=2;
    for(int i=0;i<70;++i) tick();
    assert(local.rotation.y>0);
    // Crosshair follows the target even when BODY orientation is stale and the
    // camera's raw axes are swapped, inverted, and scaled (touch-like model).
    config(moduleId,"autoCalibrate","true");
    local.rotation={-40,120}; // Intentionally wrong body orientation.
    fakehost::freezeBody=true;
    fakehost::cameraPose={}; fakehost::pendingCamera={};
    fakehost::response0={0,-2}; fakehost::response1={-.5f,0};
    for(int i=0;i<260;++i) tick();
    auto movingDesired=aim::towards({0,1.68f,0},{-1,.385f,3});
    assert(movingDesired);
    float calibratedError=aim::magnitude(aim::error({fakehost::cameraPose.x,fakehost::cameraPose.y},*movingDesired));
    assert(calibratedError<1.f);
    auto crosshairHits=[&] {
        const auto ray=static_cast<sdk::Level*>(reinterpret_cast<void*>(level.data()))->viewRay();
        if(ray.direction.z<=0) return false;
        const float t=(3.f-ray.origin.z)/ray.direction.z;
        const float x=ray.origin.x+ray.direction.x*t;
        const float y=ray.origin.y+ray.direction.y*t;
        return x>=chicken.bounds.min.x && x<=chicken.bounds.max.x &&
            y>=chicken.bounds.min.y && y<=chicken.bounds.max.y;
    };
    assert(crosshairHits());
    assert(local.rotation.x==-40 && local.rotation.y==120); // No body-field aiming.
    // Real touch/mouse input is forwarded, then tracking re-centres the live view.
    fakehost::physicalLook(local.ptr(),{.4f,-.2f});
    for(int i=0;i<100;++i) tick();
    assert(aim::magnitude(aim::error({fakehost::cameraPose.x,fakehost::cameraPose.y},*movingDesired))<1.f);
    assert(crosshairHits());
    config(moduleId,"head","true"); // Re-calibrate, then aim at the head fraction.
    for(int i=0;i<220;++i) tick();
    auto headDesired=aim::towards({0,1.68f,0},{-1,.63f,3});
    assert(headDesired);
    assert(aim::magnitude(aim::error({fakehost::cameraPose.x,fakehost::cameraPose.y},*headDesired))<1.f);
    assert(crosshairHits());
    // Regression: missing pick-ray data no longer blocks first-person tracking.
    fakehost::publishView=false;
    sdk::ViewRay emptyRay{};
    std::memcpy(level.data()+sdk::offsets::Level::mHitResultWrapper,&emptyRay,sizeof(emptyRay));
    local.rotation=fakehost::cameraPose;
    // A frozen fallback must fail calibration after one bounded probe, not spin.
    before=fakehost::turns;
    for(int i=0;i<110;++i) tick();
    assert(fakehost::turns==before+1);
    before=fakehost::turns;
    for(int i=0;i<10;++i) tick();
    assert(fakehost::turns==before);
    // Normal first-person rotation does update: calibrate and converge with the
    // same swapped/inverted/scaled native axes, even with an all-zero pick ray.
    fakehost::freezeBody=false;
    fakehost::cameraPose={}; local.rotation={}; fakehost::pendingCamera={};
    toggle(moduleId,false); toggle(moduleId,true);
    for(int i=0;i<260;++i) tick();
    assert(fakehost::turns>before+2);
    assert(aim::magnitude(aim::error({fakehost::cameraPose.x,fakehost::cameraPose.y},*headDesired))<1.f);
    // Compare the actual synthetic camera against the entity, not a pick-ray fixture.
    constexpr float rad=3.14159265358979323846f/180.f;
    const float yaw=fakehost::cameraPose.y*rad, pitch=fakehost::cameraPose.x*rad;
    const float dz=std::cos(yaw)*std::cos(pitch);
    const float hitX=-std::sin(yaw)*std::cos(pitch)*3.f/dz;
    const float hitY=1.68f-std::sin(pitch)*3.f/dz;
    assert(hitX>=chicken.bounds.min.x && hitX<=chicken.bounds.max.x);
    assert(hitY>=chicken.bounds.min.y && hitY<=chicken.bounds.max.y);
    // The master switch prevents new requests even while idle tracking is active.
    toggle(moduleId,false);
    before=fakehost::turns;
    for(int i=0;i<5;++i) tick();
    assert(fakehost::turns==before);
    fakehost::integrateCamera=false;
    assert(mod.disable(context));
    assert(fakehost::hookCount()==0);
    assert(fakehost::originalTicks>0);
}
