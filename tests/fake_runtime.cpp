// Synthetic BedrockTools ABI fixture, NOT game code or a compatibility test.
#include <bedrocktools/Api.hpp>
#include <bedrocktools/sdk/Types.hpp>
#include <vector>
#include <mutex>
#include <algorithm>
namespace {
using namespace bedrocktools;
struct Listener { std::uint64_t id; events::EventType type; api::EventCallback callback; void* user; };
std::mutex mutex;
std::vector<Listener> listeners;
std::vector<void*> actors;
std::uint64_t nextId=1;
int turns=0;
sdk::Vec2 lastDelta{};
bool blocked=false;
std::vector<void*> list(void*) { return actors; }
bool player(void* a) { return a==actors.back(); }
bool invisible(void*) { return false; }
bool solid(void*,const sdk::BlockPos&) { return blocked; }
void turn(void*,sdk::Vec2* delta) { ++turns; lastDelta=*delta; }
std::uintptr_t resolve(std::uint16_t id) {
    using Id=memory::SignatureId;
    switch(static_cast<Id>(id)) {
        case Id::ActorManagerList: return reinterpret_cast<std::uintptr_t>(&list);
        case Id::ActorIsPlayer: return reinterpret_cast<std::uintptr_t>(&player);
        case Id::ActorIsInvisible: return reinterpret_cast<std::uintptr_t>(&invisible);
        case Id::BlockSourceIsSolidBlockingBlock: return reinterpret_cast<std::uintptr_t>(&solid);
        case Id::LocalPlayerApplyTurnDelta: return reinterpret_cast<std::uintptr_t>(&turn);
        default: return 0;
    }
}
std::uint64_t subscribe(events::EventType type,events::EventPriority,api::EventCallback callback,void* user) {
    std::lock_guard lock(mutex);
    const auto id=nextId++;
    listeners.push_back({id,type,callback,user}); return id;
}
void unsubscribe(std::uint64_t id) {
    std::lock_guard lock(mutex);
    std::erase_if(listeners,[id](auto& e){return e.id==id;});
}
api::ApiV1 runtime{api::AbiVersion,sizeof(api::ApiV1),resolve,nullptr,subscribe,unsubscribe};
}
extern "C" {
const bedrocktools::api::ApiV1* BedrockTools_GetApi(std::uint32_t version) { return version==1?&runtime:nullptr; }
void Fake_SetActors(void** items,int count) { actors.assign(items,items+count); }
void Fake_SetBlocked(bool value) { blocked=value; }
int Fake_TurnCount() { return turns; }
int Fake_TickListenerCount() {
    std::lock_guard lock(mutex);
    return std::count_if(listeners.begin(),listeners.end(),[](auto& e){return e.type==bedrocktools::events::EventType::LocalPlayerTick;});
}
bedrocktools::sdk::Vec2 Fake_LastDelta() { return lastDelta; }
void Fake_Tick(void* player) {
    std::vector<Listener> copy;
    { std::lock_guard lock(mutex); copy=listeners; }
    bedrocktools::events::LocalPlayerTickEvent event{static_cast<bedrocktools::sdk::Player*>(player)};
    for(auto& l:copy) if(l.type==event.type) l.callback(l.type,&event,l.user);
}
}
