#pragma once
// Host-only fake of the LAUNCHER scanner/hook service, not another mod's API.
#include "../src/game/Runtime.hpp"
#include <algorithm>
#include <cassert>
#include <atomic>
#include <map>
#include <mutex>

namespace fakehost {
inline std::atomic<bool> gameReady{false};
inline std::atomic<int> missing{-1};
inline int failHook=-1, hookCalls=0, turns=0, originalTicks=0;
inline bool blocked=false;
inline game::Vec2 lastDelta{};
inline std::vector<void*> actors;
struct Hook { void* detour; };
inline std::mutex mutex;
inline std::map<void*,Hook> hooks;
inline void normalTick(void*) { ++originalTicks; }
inline std::vector<void*> list(void*) { return actors; }
inline bool player(void* a) { return !actors.empty() && a==actors.back(); }
inline bool invisible(void*) { return false; }
inline bool solid(void*,const game::BlockPos&) { return blocked; }
inline void turn(void*,game::Vec2* delta) { ++turns; lastDelta=*delta; }
// Distinct functions/addresses so all four screen hooks can be tracked independently.
inline void* containerOpen(void* a,void*,void*,void*,void*,void*,void*,void*) { return a; }
inline void* containerClose(void* a,void*,void*,void*,void*,void*,void*,void*) { return a; }
inline void* chatOpen(void* a,void*,void*,void*,void*,void*,void*,void*) { return a; }
inline void* chatClose(void* a,void*,void*,void*,void*,void*,void*,void*) { return a; }
inline void* address(game::Signature id) {
    using S=game::Signature;
    switch(id) {
        case S::NormalTick: return reinterpret_cast<void*>(&normalTick);
        case S::ActorManagerList: return reinterpret_cast<void*>(&list);
        case S::ActorIsPlayer: return reinterpret_cast<void*>(&player);
        case S::ActorIsInvisible: return reinterpret_cast<void*>(&invisible);
        case S::BlockSourceIsSolidBlockingBlock: return reinterpret_cast<void*>(&solid);
        case S::LocalPlayerApplyTurnDelta: return reinterpret_cast<void*>(&turn);
        case S::ContainerScreenControllerOpen: return reinterpret_cast<void*>(&containerOpen);
        case S::ContainerScreenControllerDtor: return reinterpret_cast<void*>(&containerClose);
        case S::ChatScreenOpen: return reinterpret_cast<void*>(&chatOpen);
        case S::ChatScreenDtor: return reinterpret_cast<void*>(&chatClose);
        default: return nullptr;
    }
}
inline void* detour(game::Signature id) {
    std::lock_guard lock(mutex);
    auto it=hooks.find(address(id));
    return it==hooks.end()?nullptr:it->second.detour;
}
inline unsigned hookCount() { std::lock_guard lock(mutex); return hooks.size(); }
inline void tick(void* local) {
    auto fn=reinterpret_cast<void(*)(void*)>(detour(game::Signature::NormalTick));
    assert(fn); fn(local);
}
inline void screen(game::Signature id,void* controller) {
    auto fn=reinterpret_cast<void*(*)(void*,void*,void*,void*,void*,void*,void*,void*)>(detour(id));
    assert(fn); fn(controller,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
}
}
namespace pl::memory {
std::unordered_map<std::string,std::uintptr_t> resolveSignatures(std::span<const std::string> requests,std::string_view module) {
    assert(module=="libminecraftpe.so");
    std::unordered_map<std::string,std::uintptr_t> results;
    if(!fakehost::gameReady) return results;
    for(auto& request:requests) for(unsigned i=0;i<game::patterns.size();++i) {
        if(static_cast<int>(i)!=fakehost::missing && request==game::patterns[i].bytes)
            results.emplace(request,reinterpret_cast<std::uintptr_t>(fakehost::address(static_cast<game::Signature>(i))));
    }
    return results;
}
int hook(void* target,void* detour,void** original,HookPriority) {
    std::lock_guard lock(fakehost::mutex);
    if(fakehost::hookCalls++==fakehost::failHook) return -1;
    assert(target && detour && original);
    *original=target;
    fakehost::hooks[target]={detour};
    return 0;
}
bool unhook(void* target,void*) {
    std::lock_guard lock(fakehost::mutex);
    return fakehost::hooks.erase(target)>0;
}
}
