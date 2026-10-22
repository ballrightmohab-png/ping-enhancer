#pragma once
#include "Game.hpp"
#include "Signatures.hpp"
#include <pl/memory/Signature.hpp>
#include <pl/memory/Hook.hpp>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace game {
// Only LeviLauncher's preloader is used. No other mod is loaded or queried.
class Runtime {
    using Tick=void(*)(void*);
    using Screen=void*(*)(void*,void*,void*,void*,void*,void*,void*,void*);
    inline static Runtime* active=nullptr;
    std::array<std::uintptr_t,patterns.size()> addresses{};
    std::array<pl::memory::HookHandle,5> hooks;
    Tick originalTick=nullptr;
    std::array<Screen,4> originalScreens{};
    std::function<void(Player*)> onTick;
    std::function<void(bool)> onUi;
    std::atomic<bool> running{false};
    std::recursive_mutex dispatchMutex;
    std::set<void*> chats,containers;
    static void tickDetour(void* player) {
        auto* self=active;
        if(!self) return;
        std::lock_guard lock(self->dispatchMutex);
        if(self->originalTick) self->originalTick(player);
        if(self->running && self->onTick) self->onTick(static_cast<Player*>(player));
    }
    template<unsigned Index>
    static void* screenDetour(void* a0,void* a1,void* a2,void* a3,void* a4,void* a5,void* a6,void* a7) {
        auto* self=active;
        if(!self) return nullptr;
        std::lock_guard lock(self->dispatchMutex);
        if(self->running) {
            auto& screens=Index<2?self->containers:self->chats;
            if constexpr(Index%2==0) screens.insert(a0);
            else screens.erase(a0);
            if(self->onUi) self->onUi(!self->chats.empty() || !self->containers.empty());
        }
        auto original=self->originalScreens[Index];
        return original?original(a0,a1,a2,a3,a4,a5,a6,a7):nullptr;
    }
public:
    ~Runtime() { stop(); if(active==this) active=nullptr; }
    // Batched scan of libminecraftpe.so; cache successful addresses locally.
    // Missing signatures never cause a guessed-offset hook installation.
    bool resolve(std::string& status) {
        std::vector<std::string> pending;
        for(unsigned i=0;i<patterns.size();++i)
            if(!addresses[i]) pending.emplace_back(patterns[i].bytes);
        auto results=pl::memory::resolveSignatures(pending,"libminecraftpe.so");
        status="Missing game signatures:";
        bool complete=true;
        for(unsigned i=0;i<patterns.size();++i) {
            if(!addresses[i]) {
                auto it=results.find(std::string(patterns[i].bytes));
                if(it!=results.end()) addresses[i]=it->second;
            }
            if(!addresses[i]) { status+=' '; status+=patterns[i].name; complete=false; }
        }
        if(complete) status="Signatures resolved";
        return complete;
    }
    template<class Function> Function function(Signature id) const {
        return reinterpret_cast<Function>(addresses[static_cast<unsigned>(id)]);
    }
    bool install(std::function<void(Player*)> tick,std::function<void(bool)> ui,std::string& status) {
        if(running) return true;
        for(auto address:addresses) if(!address) {
            status="Refusing to hook: game signatures are incomplete"; return false;
        }
        if(active && active!=this) { status="Another aim runtime already owns hooks"; return false; }
        active=this; onTick=std::move(tick); onUi=std::move(ui);
        void* detours[]={reinterpret_cast<void*>(&screenDetour<0>),reinterpret_cast<void*>(&screenDetour<1>),
            reinterpret_cast<void*>(&screenDetour<2>),reinterpret_cast<void*>(&screenDetour<3>)};
        constexpr Signature ids[]={Signature::ContainerScreenControllerOpen,Signature::ContainerScreenControllerDtor,
            Signature::ChatScreenOpen,Signature::ChatScreenDtor};
        for(unsigned i=0;i<4;++i) {
            hooks[i]=pl::memory::HookHandle(function<void*>(ids[i]),detours[i],reinterpret_cast<void**>(&originalScreens[i]));
            if(!hooks[i].installed()) { status="Screen hook installation failed"; stop(); return false; }
        }
        // Publish all callbacks/function pointers before the tick hook becomes live.
        running=true;
        hooks[4]=pl::memory::HookHandle(function<void*>(Signature::NormalTick),reinterpret_cast<void*>(&tickDetour),
            reinterpret_cast<void**>(&originalTick));
        if(!hooks[4].installed()) { status="Local player tick hook installation failed"; stop(); return false; }
        status="Standalone game hooks active";
        return true;
    }
    void stop() {
        running=false;
        // Remove without holding dispatchMutex: unhook may wait for a callback.
        for(auto it=hooks.rbegin();it!=hooks.rend();++it) it->reset();
        std::lock_guard lock(dispatchMutex);
        onTick={}; onUi={}; chats.clear(); containers.clear();
        // Retain originals/active for a detour already entering during unhook.
    }
};
}
