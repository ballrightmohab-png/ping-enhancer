// Build-time stub of libpreloader.so for linking LeviFSR AI.
//
// LeviLaunchroid injects the real libpreloader.so (with its pl::* runtime
// APIs) into the Minecraft process and loads mods with RTLD_GLOBAL, so a mod
// only needs to *link* against libpreloader.so and let the runtime resolve the
// symbols. This stub is used purely at build time to (a) emit the correct
// DT_NEEDED libpreloader.so entry and (b) produce symbol references with the
// same NDK/libc++ mangling as the real preloader.
//
// None of these functions is ever called from the stub, and the stub is never
// shipped inside the .levipack.
//
// IMPORTANT: keep the list of referenced symbols in sync with src/Mod.cpp and
// src/RenderPipeline.cpp so that the produced undefined symbols match what
// libpreloader.so exports.

#include <pl/Config.hpp>
#include <pl/Logger.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/ModMenuConfig.hpp>
#include <pl/memory/Hook.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace pl::mod {

NativeMod *NativeMod::current() noexcept { return nullptr; }

} // namespace pl::mod

namespace pl::log {

Logger &Logger::getOrCreate(std::string name) {
  (void)name;
  // The real implementation owns a process wide registry. The stub only has
  // to provide the symbol; this object is never used at runtime.
  static Logger stub("preloader-stub");
  return stub;
}

} // namespace pl::log

namespace pl::memory {

int hook(FuncPtr target, FuncPtr detour, FuncPtr *originalFunc,
         HookPriority priority) {
  (void)target;
  (void)detour;
  (void)originalFunc;
  (void)priority;
  return -1;
}

bool unhook(FuncPtr target, FuncPtr detour) {
  (void)target;
  (void)detour;
  return false;
}

} // namespace pl::memory

namespace pl::modmenu {

bool registerModule(const ModuleInfo &info) {
  (void)info;
  return false;
}

void unregisterModule(std::string_view moduleId) { (void)moduleId; }

void setModuleEnabled(std::string_view moduleId, bool enabled) {
  (void)moduleId;
  (void)enabled;
}

void submitDrawCommands(std::string_view moduleId,
                        std::span<const DrawCommand> commands) {
  (void)moduleId;
  (void)commands;
}

bool registerFont(std::string_view fontId,
                  std::span<const unsigned char> ttfData) {
  (void)fontId;
  (void)ttfData;
  return false;
}

bool registerImage(std::string_view imageId,
                   std::span<const unsigned char> imageData, int width,
                   int height) {
  (void)imageId;
  (void)imageData;
  (void)width;
  (void)height;
  return false;
}

bool registerButton(const ButtonInfo &info) {
  (void)info;
  return false;
}

void unregisterButton(std::string_view buttonId) { (void)buttonId; }

bool setConfigSchemaJson(std::string_view moduleId,
                         std::string_view schemaJson) {
  (void)moduleId;
  (void)schemaJson;
  return false;
}

void clearConfigSchema(std::string_view moduleId) { (void)moduleId; }

} // namespace pl::modmenu
