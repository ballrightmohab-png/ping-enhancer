// Compile against upstream headers, but replace the Android lifecycle context.
#include <pl/ModMenu.hpp>
#include <cassert>
#include <optional>
namespace {
std::optional<pl::modmenu::ModuleInfo> registeredMenu;
}
namespace pl::modmenu {
bool registerModule(const ModuleInfo& info) { registeredMenu=info; return true; }
void submitDrawCommands(std::string_view, std::span<const DrawCommand>) {}
void unregisterModule(std::string_view) { registeredMenu.reset(); }
}
#include "../src/Mod.cpp"
int main() {
    pl::mod::ModContext context;
    auto& mod=AdaptiveAim::instance();
    // No libBedrockTools.so loaded: dependency failure must NOT hide controls.
    assert(mod.enable(context));
    assert(registeredMenu);
    assert(!registeredMenu->defaultEnabled);
    const auto& entries=registeredMenu->configs;
    auto find=[&](const char* key) -> const pl::modmenu::ConfigEntry& {
        for(const auto& e:entries) if(e.key==key) return e;
        assert(false && "Missing required menu control");
        std::abort();
    };
    using Type=pl::modmenu::ConfigType;
    assert(find("players").type==Type::Toggle);
    assert(find("mobs").type==Type::Toggle);
    assert(find("strength").type==Type::SliderFloat);
    assert(find("strength").minValue=="0");
    assert(find("strength").maxValue=="100");
    assert(find("players").defaultValue=="true");
    assert(find("mobs").defaultValue=="false");
    auto toggle=registeredMenu->onToggle;
    auto config=registeredMenu->onConfigChanged;
    config(moduleId,"strength","75");
    config(moduleId,"players","false");
    config(moduleId,"mobs","true");
    toggle(moduleId,true); // Retry missing runtime without removing the menu.
    assert(registeredMenu);
    toggle(moduleId,false);
    assert(mod.disable(context));
    assert(!registeredMenu);
    assert(mod.enable(context)); // Reload must re-create controls.
    assert(registeredMenu);
    assert(mod.disable(context));
}
