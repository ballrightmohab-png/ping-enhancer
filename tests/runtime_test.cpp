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
    config(moduleId,"players","false");
    config(moduleId,"mobs","true");
    config(moduleId,"strength","100");
    config(moduleId,"fov","60");
    toggle(moduleId,true); // Minecraft deliberately not available yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::lock_guard lock(statusMutex); assert(lastStatus.find("Missing game signatures")!=std::string::npos); }
    fakehost::gameReady=true;
    for(int n=0;n<200 && fakehost::hookCount()!=5;++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(fakehost::hookCount()==5); // Automatic game hook installation.
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
    auto tick=[&]{std::this_thread::sleep_for(std::chrono::milliseconds(10)); fakehost::tick(local.ptr());};
    tick(); tick();
    assert(fakehost::turns>0);
    assert(fakehost::lastDelta.x>0); // Downward pitch toward chicken torso.
    int before=fakehost::turns;
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
    assert(mod.disable(context));
    assert(fakehost::hookCount()==0);
    assert(fakehost::originalTicks>0);
}
