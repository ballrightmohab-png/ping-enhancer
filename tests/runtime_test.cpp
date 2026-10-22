#include <pl/ModMenu.hpp>
#include <cassert>
#include <optional>
#include <mutex>
#include <array>
#include <thread>
#include <cstring>
#include <dlfcn.h>
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
int main(int argc,char** argv) {
    assert(argc==2);
    pl::mod::ModContext context;
    auto& mod=AdaptiveAim::instance();
    assert(mod.enable(context));
    auto config=registeredMenu->onConfigChanged;
    auto toggle=registeredMenu->onToggle;
    config(moduleId,"players","false");
    config(moduleId,"mobs","true");
    config(moduleId,"strength","100");
    config(moduleId,"fov","60");
    toggle(moduleId,true); // Dependency deliberately not loaded yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::lock_guard lock(statusMutex); assert(lastStatus.find("Missing BedrockTools")!=std::string::npos); }
    void* handle=dlopen(argv[1],RTLD_NOW|RTLD_GLOBAL);
    assert(handle);
    auto setActors=reinterpret_cast<void(*)(void**,int)>(dlsym(handle,"Fake_SetActors"));
    auto setBlocked=reinterpret_cast<void(*)(bool)>(dlsym(handle,"Fake_SetBlocked"));
    auto emit=reinterpret_cast<void(*)(void*)>(dlsym(handle,"Fake_Tick"));
    auto count=reinterpret_cast<int(*)()>(dlsym(handle,"Fake_TurnCount"));
    auto listenerCount=reinterpret_cast<int(*)()>(dlsym(handle,"Fake_TickListenerCount"));
    auto delta=reinterpret_cast<sdk::Vec2(*)()>(dlsym(handle,"Fake_LastDelta"));
    assert(setActors && setBlocked && emit && count && delta && listenerCount);
    for(int n=0;n<100 && !listenerCount();++n) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(listenerCount()==1); // Automatic recovery: no additional toggle.
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
    setActors(actors,2);
    auto tick=[&]{std::this_thread::sleep_for(std::chrono::milliseconds(10)); emit(local.ptr());};
    tick(); tick();
    assert(count()>0);
    assert(delta().x>0); // Downward pitch toward chicken torso.
    int before=count();
    config(moduleId,"mobs","false"); tick(); assert(count()==before);
    config(moduleId,"mobs","true");
    config(moduleId,"strength","0"); tick(); assert(count()==before);
    config(moduleId,"strength","100");
    setBlocked(true); tick(); assert(count()==before);
    setBlocked(false);
    config(moduleId,"fov","1"); tick(); assert(count()==before);
    config(moduleId,"fov","60"); tick(); assert(count()>before);
    before=count();
    // Treat the nearby fixture as a player: mobs ON must not override players OFF.
    void* swapped[]={otherPlayer.ptr(),chicken.ptr()}; setActors(swapped,2);
    tick(); assert(count()==before);
    config(moduleId,"players","true"); tick(); assert(count()>before);
    assert(mod.disable(context));
    assert(listenerCount()==0);
    dlclose(handle);
}
