#pragma once
#include "Game.hpp"
#include "Signatures.hpp"
#include <pl/memory/Signature.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Vtable.hpp>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace game {
// Standalone hooks using LeviLauncher's host APIs, not another mod's runtime.
class Runtime {
    using Tick=void(*)(void*);
    using Menu=bool(*)(void*);
    using Turn=void(*)(void*,Vec2*);
    inline static Runtime* active=nullptr;
    std::array<std::uintptr_t,patterns.size()> addresses{};
    std::uintptr_t menuAddress=0;
    // Same RTTI/slot rule used by LeviLauncher v1.5.27 for MC >= 1.26.50.
    static constexpr unsigned menuSlot=151;
    std::array<pl::memory::HookHandle,3> hooks;
    Tick originalTick=nullptr;
    Menu originalMenu=nullptr;
    Turn originalTurn=nullptr;
    std::function<void(Player*)> onTick;
    std::function<void(bool)> onUi;
    std::atomic<bool> running{false};
    std::recursive_mutex dispatchMutex;
    static void tickDetour(void* player) {
        auto* self=active;
        if(!self) return;
        std::lock_guard lock(self->dispatchMutex);
        if(self->originalTick) self->originalTick(player);
        if(self->running && self->onTick) self->onTick(static_cast<Player*>(player));
    }
    static void turnDetour(void* player,Vec2* input) {
        auto* self=active;
        if(!self) return;
        std::lock_guard lock(self->dispatchMutex);
        // frame-gen's original-callback pattern. Physical input is unchanged,
        // including when aim is disabled; do not generate a second correction.
        if(self->originalTurn) self->originalTurn(player,input);
    }
    static bool menuDetour(void* client) {
        auto* self=active;
        if(!self) return true;
        std::lock_guard lock(self->dispatchMutex);
        // Preserve the actual game return value. Missing originals fail closed.
        const bool showing=self->originalMenu?self->originalMenu(client):true;
        if(self->running && self->onUi) self->onUi(showing);
        return showing;
    }
public:
    ~Runtime() { stop(); if(active==this) active=nullptr; }
    bool resolve(std::string& status) {
        std::vector<std::string> pending;
        for(unsigned i=0;i<patterns.size();++i)
            if(!addresses[i]) pending.emplace_back(patterns[i].bytes);
        auto results=pl::memory::resolveSignatures(pending,"libminecraftpe.so");
        status="Missing game functions:";
        bool complete=true;
        for(unsigned i=0;i<patterns.size();++i) {
            if(!addresses[i]) {
                auto it=results.find(std::string(patterns[i].bytes));
                if(it!=results.end()) addresses[i]=it->second;
            }
            if(!addresses[i]) { status+=' '; status+=patterns[i].name; complete=false; }
        }
        if(!menuAddress)
            menuAddress=pl::memory::resolveVtableFunction("14ClientInstance",menuSlot,"libminecraftpe.so");
        if(!menuAddress) { status+=" ClientInstance::isShowingMenu (RTTI slot 151)"; complete=false; }
        if(complete) status="Game functions resolved (launcher menu-state path)";
        return complete;
    }
    template<class Function> Function function(Signature id) const {
        return reinterpret_cast<Function>(addresses[static_cast<unsigned>(id)]);
    }
    bool install(std::function<void(Player*)> tick,std::function<void(bool)> ui,std::string& status) {
        if(running) return true;
        if(!menuAddress) { status="Refusing to hook: menu-state function unavailable"; return false; }
        for(auto address:addresses) if(!address) {
            status="Refusing to hook: game signatures are incomplete"; return false;
        }
        if(active && active!=this) { status="Another aim runtime already owns hooks"; return false; }
        active=this; onTick=std::move(tick); onUi=std::move(ui);
        // No assuming gameplay is active before we observe the menu query.
        if(onUi) onUi(true);
        hooks[0]=pl::memory::HookHandle(reinterpret_cast<void*>(menuAddress),reinterpret_cast<void*>(&menuDetour),
            reinterpret_cast<void**>(&originalMenu));
        if(!hooks[0].installed()) { status="Menu-state hook installation failed"; stop(); return false; }
        hooks[1]=pl::memory::HookHandle(function<void*>(Signature::LocalPlayerApplyTurnDelta),
            reinterpret_cast<void*>(&turnDetour),reinterpret_cast<void**>(&originalTurn));
        if(!hooks[1].installed()) { status="Camera turn hook installation failed"; stop(); return false; }
        running=true;
        hooks[2]=pl::memory::HookHandle(function<void*>(Signature::NormalTick),reinterpret_cast<void*>(&tickDetour),
            reinterpret_cast<void**>(&originalTick));
        if(!hooks[2].installed()) { status="Local player tick hook installation failed"; stop(); return false; }
        status="Standalone hooks active (launcher menu-state path)";
        return true;
    }
    bool sendAutomaticTurn(Player* player,Vec2 input) {
        std::lock_guard lock(dispatchMutex);
        if(!running || !originalTurn || !player) return false;
        // A game-tick-driven command uses the captured original callback, just
        // like frame-gen's detour, without recursively re-entering our own hook.
        originalTurn(player,&input);
        return true;
    }
    void stop() {
        running=false;
        for(auto it=hooks.rbegin();it!=hooks.rend();++it) it->reset();
        std::lock_guard lock(dispatchMutex);
        onTick={}; onUi={};
    }
};
}
