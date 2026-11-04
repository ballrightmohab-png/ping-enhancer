// Compile against upstream headers, but replace the Android lifecycle context.
#include <pl/ModMenu.hpp>
#include <cassert>
#include <optional>
namespace {
std::optional<pl::modmenu::ModuleInfo> registeredMenu;
}
namespace pl::modmenu {
bool setConfigSchemaJson(std::string_view,std::string_view schema) {
    assert(schema.find("Tracking status (menu only)")!=std::string_view::npos);
    assert(schema.find("Target mobs")!=std::string_view::npos);
    return true;
}
bool registerModule(const ModuleInfo& info) { registeredMenu=info; return true; }
void submitDrawCommands(std::string_view, std::span<const DrawCommand> commands) { assert(commands.empty()); }
void unregisterModule(std::string_view) { registeredMenu.reset(); }
}
#include "FakeHost.hpp"
#include "../src/Mod.cpp"
int main() {
    aim::Settings values;
    values.strength=75; values.mobs=true;
    assert(aim::settingValue(values,"strength")=="75");
    assert(aim::settingValue(values,"mobs")=="true");
    const auto schema=aim::menuSchema(values,"Missing game functions: test\"name");
    assert(schema.find("retry")!=std::string::npos);
    assert(schema.find("Missing game functions")!=std::string::npos);
    assert(schema.find("test\\\"name")!=std::string::npos); // JSON escapes diagnostic text.
    pl::mod::ModContext context;
    auto& mod=AdaptiveAim::instance();
    // No Minecraft loaded: missing signatures must NOT hide controls.
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
    assert(find("autoCalibrate").type==Type::Toggle);
    assert(find("cameraSmoothing").type==Type::Toggle);
    assert(find("smoothness").type==Type::SliderFloat);
    assert(find("yawFirst").defaultValue=="false");
    assert(find("players").type==Type::Toggle);
    assert(find("mobs").type==Type::Toggle);
    assert(find("strength").type==Type::SliderInt);
    assert(find("strength").displayName.find("100 = AIM BOT!")!=std::string::npos);
    assert(find("strength").minValue=="10");
    assert(find("strength").maxValue=="100");
    assert(find("players").defaultValue=="true");
    assert(find("mobs").defaultValue=="false");
    auto toggle=registeredMenu->onToggle;
    auto config=registeredMenu->onConfigChanged;
    config(moduleId,"strength","75");
    config(moduleId,"players","false");
    config(moduleId,"mobs","true");
    toggle(moduleId,true); // Retry missing game without removing the menu.
    assert(registeredMenu);
    toggle(moduleId,false);
    assert(mod.disable(context));
    assert(!registeredMenu);
    assert(mod.enable(context)); // Reload must re-create controls.
    assert(registeredMenu);
    assert(mod.disable(context));
}
