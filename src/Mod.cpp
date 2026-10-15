// LeviFSR AI - module lifecycle, LeviLaunchroid Mod Menu integration and the
// diagnostic overlay.
//
// The module follows the native mod contract of LeviLaunchroid:
//
//   load()    - reads the typed configuration, probes the device
//   enable()  - installs the presentation hook and registers the Mod Menu
//   disable() - removes the Mod Menu entries and restores the game rendering
//   unload()  - releases the configuration
//
// Every control that is exposed in the launcher is connected to the real
// implementation. Controls that cannot work on the current device are
// reported as unavailable instead of pretending to be active.

#include "AiBackend.hpp"
#include "Config.hpp"
#include "RenderPipeline.hpp"

#include <pl/Config.hpp>
#include <pl/Logger.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/ModMenuConfig.hpp>

#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace levifsr;

constexpr const char *kModuleId = "levifsr_ai";
constexpr const char *kModId = "levifsr_ai";

// The V2 schema API is resolved dynamically: a launcher that ships an older
// preloader runtime simply keeps the classic configuration list.
constexpr const char *kSetConfigSchemaJsonSymbol =
    "_ZN2pl8modmenu19setConfigSchemaJsonESt17basic_string_viewIcSt11char_traitsIcEE";

// ---------------------------------------------------------------------------
// Configuration keys
// ---------------------------------------------------------------------------
constexpr const char *kKeyMaster = "master_enabled";
constexpr const char *kKeyUpscaling = "upscaling_enabled";
constexpr const char *kKeyMode = "upscale_mode";
constexpr const char *kKeyScale = "render_scale";
constexpr const char *kKeySharpening = "sharpening";
constexpr const char *kKeyDynamicResolution = "dynamic_resolution";
constexpr const char *kKeyTargetFps = "target_fps";
constexpr const char *kKeyGpuOptimization = "gpu_optimization";
constexpr const char *kKeyLowLatency = "low_latency_mode";
constexpr const char *kKeyInternalResolution = "internal_resolution";
constexpr const char *kKeySpatial = "spatial_upscaling";
constexpr const char *kKeyDetailRecovery = "detail_recovery";
constexpr const char *kKeyFramePacing = "frame_pacing";
constexpr const char *kKeyStabilization = "resolution_stabilization";
constexpr const char *kKeyResources = "resource_management";
constexpr const char *kKeyDamageSwap = "damage_swap";
constexpr const char *kKeyMinScale = "min_render_scale";
constexpr const char *kKeyMaxScale = "max_render_scale";
constexpr const char *kKeyOverlay = "diagnostic_overlay";
constexpr const char *kKeyGpuTimings = "gpu_timings";
constexpr const char *kKeyVerbose = "verbose_logging";
constexpr const char *kKeySafeMode = "safe_mode";
constexpr const char *kKeyRestore = "restore_defaults";

// ---------------------------------------------------------------------------
// Value parsing helpers
// ---------------------------------------------------------------------------
bool parseBool(std::string_view value, bool fallback) {
  if (value == "true" || value == "1" || value == "on" || value == "yes") {
    return true;
  }
  if (value == "false" || value == "0" || value == "off" || value == "no") {
    return false;
  }
  return fallback;
}

int parseInt(std::string_view value, int fallback) {
  if (value.empty()) {
    return fallback;
  }
  char *end = nullptr;
  const std::string text(value);
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str()) {
    return fallback;
  }
  return static_cast<int>(parsed);
}

// The mode is transported either as the index of the radio entry (classic
// menu) or as the name of the choice (V2 menu).
int parseMode(std::string_view value, int fallback) {
  if (value == "native" || value == "0") {
    return static_cast<int>(UpscaleMode::Native);
  }
  if (value == "quality" || value == "1") {
    return static_cast<int>(UpscaleMode::Quality);
  }
  if (value == "balanced" || value == "2") {
    return static_cast<int>(UpscaleMode::Balanced);
  }
  if (value == "performance" || value == "3") {
    return static_cast<int>(UpscaleMode::Performance);
  }
  if (value == "ultra" || value == "ultra_performance" || value == "4") {
    return static_cast<int>(UpscaleMode::UltraPerformance);
  }
  return fallback;
}

std::string modeValue(int mode) {
  switch (clampMode(mode)) {
  case UpscaleMode::Native:
    return "native";
  case UpscaleMode::Quality:
    return "quality";
  case UpscaleMode::Balanced:
    return "balanced";
  case UpscaleMode::Performance:
    return "performance";
  case UpscaleMode::UltraPerformance:
    return "ultra";
  }
  return "balanced";
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------
class LeviFsrMod {
public:
  static LeviFsrMod &instance() {
    static LeviFsrMod mod;
    return mod;
  }

  LeviFsrMod() : mSelf(*ll::mod::NativeMod::current()) {}

  [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

  bool load() {
    auto &self = getSelf();
    std::error_code ec;
    std::filesystem::create_directories(self.getConfigDir(), ec);
    std::filesystem::create_directories(self.getDataDir(), ec);

    mConfigFile.emplace();
    if (!mConfigFile->load()) {
      self.getLogger().error("Failed to load the typed configuration");
      mConfigFile.reset();
      return false;
    }
    mConfig = mConfigFile->value();
    if (normalize(mConfig)) {
      mConfigFile->save();
    }

    mAiStatus = probeAiBackend(self.getResourceDir() / "models");

    self.getLogger().info(
        "LeviFSR AI loaded: mode={} scale={}% sharpening={} ai={}",
        modeLabel(clampMode(mConfig.upscaleMode)), mConfig.renderScale,
        mConfig.sharpening, mAiStatus.detail);
    return true;
  }

  bool enable() {
    auto &self = getSelf();

    if (!RenderPipeline::instance().install()) {
      self.getLogger().error(
          "The graphics stack does not allow the presentation hook: {}",
          RenderPipeline::instance().statusLine());
    }
    RenderPipeline::instance().setFrameCallback([this]() { onFrame(); });
    applyRuntimeSettings();

    const bool registered = registerMenu();
    if (!registered) {
      self.getLogger().error("Failed to register the Mod Menu module");
      return false;
    }
    self.getLogger().info("LeviFSR AI enabled");
    return true;
  }

  bool disable() {
    RenderPipeline::instance().setFrameCallback(nullptr);
    unregisterMenu();
    RenderPipeline::instance().uninstall();
    getSelf().getLogger().info("LeviFSR AI disabled");
    return true;
  }

  bool unload() {
    unregisterMenu();
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mConfigFile.reset();
    }
    getSelf().getLogger().info("LeviFSR AI unloaded");
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
  std::optional<pl::config::ConfigFile<Config>> mConfigFile;
  std::mutex mMutex;
  Config mConfig;
  AiBackendStatus mAiStatus;
  std::atomic<uint64_t> mLastOverlayUpdate{0};
  bool mModuleRegistered = false;
  bool mSchemaPublished = false;

  // -------------------------------------------------------------------------
  // Menu registration
  // -------------------------------------------------------------------------
  bool registerMenu() {
    auto &self = getSelf();
    const Config config = snapshotConfig();

    pl::modmenu::ModuleBuilder builder(kModuleId, "LeviFSR AI");
    builder.modId(self.getId());
    builder.description(
        "Reduced internal resolution with an edge adaptive reconstruction "
        "pass, adaptive dynamic resolution and low latency frame pacing for "
        "Minecraft Bedrock.");
    builder.defaultEnabled(config.masterEnabled);
    builder.onToggle([](std::string_view moduleId, bool enabled) {
      if (moduleId == kModuleId) {
        LeviFsrMod::instance().handleToggle(enabled);
      }
    });
    builder.onConfigChanged([](std::string_view moduleId, std::string_view key,
                               std::string_view value) {
      if (moduleId == kModuleId) {
        LeviFsrMod::instance().handleConfigChanged(key, value);
      }
    });

    builder.config(kKeyMaster, "Master Enable",
                   pl::modmenu::ConfigType::Toggle,
                   config.masterEnabled ? "true" : "false");
    builder.config(kKeyUpscaling, "Upscaling",
                   pl::modmenu::ConfigType::Toggle,
                   config.upscalingEnabled ? "true" : "false");
    builder.config(kKeyMode, "Upscaling Mode",
                   pl::modmenu::ConfigType::Radio,
                   std::to_string(config.upscaleMode),
                   "Native Resolution,Quality,Balanced,Performance,Ultra "
                   "Performance");
    builder.config(kKeyScale, "Render Scale",
                   pl::modmenu::ConfigType::Radio,
                   std::to_string(config.renderScale), "100,90,80,70,60,50");
    builder.config(kKeySharpening, "Sharpening",
                   pl::modmenu::ConfigType::SliderInt,
                   std::to_string(config.sharpening), "0", "100");
    builder.config(kKeyDynamicResolution, "Dynamic Resolution",
                   pl::modmenu::ConfigType::Toggle,
                   config.dynamicResolution ? "true" : "false");
    builder.config(kKeyTargetFps, "Target FPS",
                   pl::modmenu::ConfigType::Radio,
                   std::to_string(config.targetFps), "30,45,60,90,120");
    builder.config(kKeyGpuOptimization, "GPU Optimization",
                   pl::modmenu::ConfigType::Toggle,
                   config.gpuOptimization ? "true" : "false");
    builder.config(kKeyLowLatency, "Low-Latency Mode",
                   pl::modmenu::ConfigType::Toggle,
                   config.lowLatencyMode ? "true" : "false");
    builder.config(kKeyInternalResolution, "Internal Resolution Control",
                   pl::modmenu::ConfigType::Toggle,
                   config.internalResolutionControl ? "true" : "false");
    builder.config(kKeySpatial, "Spatial Upscaling",
                   pl::modmenu::ConfigType::Toggle,
                   config.spatialUpscaling ? "true" : "false");
    builder.config(kKeyDetailRecovery, "Detail Recovery",
                   pl::modmenu::ConfigType::Toggle,
                   config.detailRecovery ? "true" : "false");
    builder.config(kKeyFramePacing, "Frame Pacing Optimization",
                   pl::modmenu::ConfigType::Toggle,
                   config.framePacingOptimization ? "true" : "false");
    builder.config(kKeyStabilization, "Render Resolution Stabilization",
                   pl::modmenu::ConfigType::Toggle,
                   config.resolutionStabilization ? "true" : "false");
    builder.config(kKeyResources, "Resource and History Buffer Management",
                   pl::modmenu::ConfigType::Toggle,
                   config.resourceManagement ? "true" : "false");
    builder.config(kKeyDamageSwap, "Damage Based Swap",
                   pl::modmenu::ConfigType::Toggle,
                   config.damageSwap ? "true" : "false");
    builder.config(kKeyMinScale, "Minimum Render Scale",
                   pl::modmenu::ConfigType::SliderInt,
                   std::to_string(config.minRenderScale), "50", "100");
    builder.config(kKeyMaxScale, "Maximum Render Scale",
                   pl::modmenu::ConfigType::SliderInt,
                   std::to_string(config.maxRenderScale), "50", "100");
    builder.config(kKeyOverlay, "Diagnostic Overlay",
                   pl::modmenu::ConfigType::Toggle,
                   config.debugOverlay ? "true" : "false");
    builder.config(kKeyGpuTimings, "GPU Timings",
                   pl::modmenu::ConfigType::Toggle,
                   config.gpuTimings ? "true" : "false");
    builder.config(kKeyVerbose, "Verbose Logging",
                   pl::modmenu::ConfigType::Toggle,
                   config.verboseLogging ? "true" : "false");
    builder.config(kKeySafeMode, "Safe Mode",
                   pl::modmenu::ConfigType::Toggle,
                   config.safeMode ? "true" : "false");

    mModuleRegistered = builder.registerModule();
    publishSchema();
    return mModuleRegistered;
  }

  void unregisterMenu() {
    if (mSchemaPublished) {
      if (auto clear = reinterpret_cast<void (*)(std::string_view)>(
              dlsym(RTLD_DEFAULT,
                    "_ZN2pl8modmenu17clearConfigSchemaESt17basic_string_viewIcS"
                    "t11char_traitsIcEE"))) {
        clear(kModuleId);
      }
      mSchemaPublished = false;
    }
    pl::modmenu::unregisterModule(kModuleId);
    mModuleRegistered = false;
  }

  // Builds the V2 schema of the module. The schema is the rich description of
  // the settings: categories, sections, conditions and live values.
  void publishSchema() {
    using pl::modmenu::ConfigConditionOpV2;
    using pl::modmenu::ConfigControlTypeV2;
    using pl::modmenu::ConfigNodeV2;
    using pl::modmenu::ConfigOptionV2;
    using pl::modmenu::ConfigSchemaBuilder;

    const Config config = snapshotConfig();
    const Metrics metrics = RenderPipeline::instance().metrics();
    const auto &gpu = RenderPipeline::instance().gpuInfo();

    ConfigSchemaBuilder builder;
    builder.category("core", "LeviFSR AI",
                     "Upscaling, sharpening and frame rate control.");
    builder.category("advanced", "Advanced",
                     "Internal resolution, frame pacing and diagnostics.");
    builder.defaultCategory("core");

    // --- Core -------------------------------------------------------------
    builder.node(ConfigNodeV2{
        .id = "core_section",
        .key = "",
        .category = "core",
        .title = "Main Controls",
        .description = "",
        .section = "",
        .type = ConfigControlTypeV2::Section,
        .collapsible = false,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyMaster,
        .key = kKeyMaster,
        .category = "core",
        .title = "Master Enable",
        .description =
            "Turns the whole rendering pipeline on or off. When disabled the "
            "game renders exactly as it does without the module.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.masterEnabled ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyUpscaling,
        .key = kKeyUpscaling,
        .category = "core",
        .title = "Upscaling",
        .description =
            "Enables the reconstruction pass. Turning it off renders at the "
            "native resolution without any additional GPU work.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.upscalingEnabled ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyMode,
        .key = kKeyMode,
        .category = "core",
        .title = "Upscaling Mode",
        .description =
            "Selects the reconstruction preset. The preset sets the internal "
            "render scale and the sharpening strength.",
        .type = ConfigControlTypeV2::Choice,
        .choiceStyle = pl::modmenu::ConfigChoiceStyleV2::Segmented,
        .defaultValue = modeValue(config.upscaleMode),
        .currentValue = modeValue(config.upscaleMode),
        .hasCurrentValue = true,
        .options = {
            {.value = "native", .label = "Native", .description =
                "No reconstruction, no overhead."},
            {.value = "quality", .label = "Quality", .description =
                "90% internal scale."},
            {.value = "balanced", .label = "Balanced", .description =
                "80% internal scale."},
            {.value = "performance", .label = "Performance", .description =
                "70% internal scale."},
            {.value = "ultra", .label = "Ultra", .description =
                "60% internal scale."},
        },
    });

    builder.node(ConfigNodeV2{
        .id = kKeyScale,
        .key = kKeyScale,
        .category = "core",
        .title = "Render Scale",
        .description =
            "Resolution of the internal 3D render target. The interface is "
            "always drawn at the native resolution.",
        .type = ConfigControlTypeV2::Choice,
        .choiceStyle = pl::modmenu::ConfigChoiceStyleV2::Chips,
        .defaultValue = std::to_string(config.renderScale),
        .currentValue = std::to_string(config.renderScale),
        .hasCurrentValue = true,
        .options = {
            {.value = "100", .label = "100%", .description = "Native."},
            {.value = "90", .label = "90%", .description = ""},
            {.value = "80", .label = "80%", .description = ""},
            {.value = "70", .label = "70%", .description = ""},
            {.value = "60", .label = "60%", .description = ""},
            {.value = "50", .label = "50%", .description = ""},
        },
        .visibleWhen = {{kKeyUpscaling, ConfigConditionOpV2::Truthy, "true"}},
    });

    builder.node(ConfigNodeV2{
        .id = kKeySharpening,
        .key = kKeySharpening,
        .category = "core",
        .title = "Sharpening",
        .description =
            "Contrast adaptive sharpening strength applied by the "
            "reconstruction pass.",
        .type = ConfigControlTypeV2::SliderInt,
        .defaultValue = std::to_string(config.sharpening),
        .currentValue = std::to_string(config.sharpening),
        .hasCurrentValue = true,
        .minValue = "0",
        .maxValue = "100",
        .step = "1",
        .unit = "%",
    });

    builder.node(ConfigNodeV2{
        .id = kKeyDynamicResolution,
        .key = kKeyDynamicResolution,
        .category = "core",
        .title = "Dynamic Resolution",
        .description =
            "Adjusts the internal render scale to hold the selected frame "
            "rate. Only active while the internal resolution control is "
            "available.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.dynamicResolution ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyTargetFps,
        .key = kKeyTargetFps,
        .category = "core",
        .title = "Target FPS",
        .description = "Frame rate the dynamic resolution controller aims for.",
        .type = ConfigControlTypeV2::Choice,
        .choiceStyle = pl::modmenu::ConfigChoiceStyleV2::Segmented,
        .defaultValue = std::to_string(config.targetFps),
        .currentValue = std::to_string(config.targetFps),
        .hasCurrentValue = true,
        .options = {
            {.value = "30", .label = "30"}, {.value = "45", .label = "45"},
            {.value = "60", .label = "60"}, {.value = "90", .label = "90"},
            {.value = "120", .label = "120"},
        },
        .visibleWhen = {{kKeyDynamicResolution, ConfigConditionOpV2::Truthy,
                         "true"}},
    });

    builder.node(ConfigNodeV2{
        .id = kKeyGpuOptimization,
        .key = kKeyGpuOptimization,
        .category = "core",
        .title = "GPU Optimization",
        .description =
            "Vendor aware tuning: render target reuse, avoided render target "
            "switches and reduced shader overhead on Mali and Adreno.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.gpuOptimization ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyLowLatency,
        .key = kKeyLowLatency,
        .category = "core",
        .title = "Low-Latency Mode",
        .description =
            "Keeps the frame path free of synchronisation, blocking shader "
            "compilation, per frame allocations and CPU side image work.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.lowLatencyMode ? "true" : "false",
        .hasCurrentValue = true,
    });

    // --- Advanced ---------------------------------------------------------
    builder.node(ConfigNodeV2{
        .id = "advanced_section",
        .key = "",
        .category = "advanced",
        .title = "Advanced Settings",
        .description = "",
        .section = "",
        .type = ConfigControlTypeV2::Section,
        .collapsible = true,
    });

    // The live status is part of the description so the launcher shows what
    // the module is actually doing right now.
    const std::string status = describeStatus(metrics, gpu);

    builder.node(ConfigNodeV2{
        .id = kKeyInternalResolution,
        .key = kKeyInternalResolution,
        .category = "advanced",
        .title = "Internal Resolution Control",
        .description =
            "Renders the 3D scene into a smaller offscreen target. The game "
            "keeps drawing the interface on the native surface. The module "
            "detects the scene target at runtime and reverts automatically if "
            "it cannot be resized safely. Current status: " +
            status + ".",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.internalResolutionControl ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeySpatial,
        .key = kKeySpatial,
        .category = "advanced",
        .title = "Spatial Upscaling",
        .description =
            "Edge adaptive spatial reconstruction of the rendered frame.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.spatialUpscaling ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyDetailRecovery,
        .key = kKeyDetailRecovery,
        .category = "advanced",
        .title = "Detail Recovery",
        .description =
            "Rebuilds the high frequency detail that the upscale of the game "
            "removed, using the reduced resolution scene target as reference.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.detailRecovery ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyFramePacing,
        .key = kKeyFramePacing,
        .category = "advanced",
        .title = "Frame Pacing Optimization",
        .description =
            "Removes avoidable stalls from the frame path: no per frame "
            "allocations, no blocking queries, no shader compilation while "
            "the game is running.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.framePacingOptimization ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyStabilization,
        .key = kKeyStabilization,
        .category = "advanced",
        .title = "Render Resolution Stabilization",
        .description =
            "Prevents the dynamic resolution controller from changing the "
            "render scale more often than necessary.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.resolutionStabilization ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyResources,
        .key = kKeyResources,
        .category = "advanced",
        .title = "Resource and Buffer Management",
        .description =
            "Reuses the GPU resources of the module and releases them when "
            "the module is disabled.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.resourceManagement ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyDamageSwap,
        .key = kKeyDamageSwap,
        .category = "advanced",
        .title = "Damage Based Swap",
        .description =
            "Uses the damage based swap path when the driver supports it, "
            "which reduces the work of the presentation step.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "true",
        .currentValue = config.damageSwap ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyMinScale,
        .key = kKeyMinScale,
        .category = "advanced",
        .title = "Minimum Render Scale",
        .description = "Lower bound used by the dynamic resolution controller.",
        .type = ConfigControlTypeV2::SliderInt,
        .defaultValue = std::to_string(config.minRenderScale),
        .currentValue = std::to_string(config.minRenderScale),
        .hasCurrentValue = true,
        .minValue = "50",
        .maxValue = "100",
        .step = "5",
        .unit = "%",
        .visibleWhen = {{kKeyDynamicResolution, ConfigConditionOpV2::Truthy,
                         "true"}},
    });

    builder.node(ConfigNodeV2{
        .id = kKeyMaxScale,
        .key = kKeyMaxScale,
        .category = "advanced",
        .title = "Maximum Render Scale",
        .description = "Upper bound used by the dynamic resolution controller.",
        .type = ConfigControlTypeV2::SliderInt,
        .defaultValue = std::to_string(config.maxRenderScale),
        .currentValue = std::to_string(config.maxRenderScale),
        .hasCurrentValue = true,
        .minValue = "50",
        .maxValue = "100",
        .step = "5",
        .unit = "%",
        .visibleWhen = {{kKeyDynamicResolution, ConfigConditionOpV2::Truthy,
                         "true"}},
    });

    builder.node(ConfigNodeV2{
        .id = kKeySafeMode,
        .key = kKeySafeMode,
        .category = "advanced",
        .title = "Safe Mode",
        .description =
            "Disables every rendering change. The module then only measures "
            "and reports. Use this when the game looks wrong.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "false",
        .currentValue = config.safeMode ? "true" : "false",
        .hasCurrentValue = true,
    });

    // --- Diagnostics ------------------------------------------------------
    builder.node(ConfigNodeV2{
        .id = "diagnostics_section",
        .key = "",
        .category = "advanced",
        .title = "Diagnostics",
        .description = "",
        .section = "",
        .type = ConfigControlTypeV2::Section,
        .collapsible = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyOverlay,
        .key = kKeyOverlay,
        .category = "advanced",
        .title = "Diagnostic Overlay",
        .description =
            "Shows the measured frame rate, frame time, render scale, backend "
            "and the state of the dynamic resolution controller.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "false",
        .currentValue = config.debugOverlay ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyGpuTimings,
        .key = kKeyGpuTimings,
        .category = "advanced",
        .title = "GPU Timings",
        .description =
            "Collects the GPU time of the reconstruction pass with a timer "
            "query. This adds a small overhead and is meant for "
            "measurements.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "false",
        .currentValue = config.gpuTimings ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyVerbose,
        .key = kKeyVerbose,
        .category = "advanced",
        .title = "Verbose Logging",
        .description = "Writes additional information to the Android log.",
        .type = ConfigControlTypeV2::Toggle,
        .defaultValue = "false",
        .currentValue = config.verboseLogging ? "true" : "false",
        .hasCurrentValue = true,
    });

    builder.node(ConfigNodeV2{
        .id = kKeyRestore,
        .key = kKeyRestore,
        .category = "advanced",
        .title = "Restore Defaults",
        .description =
            "Resets every setting to the documented default configuration.",
        .type = ConfigControlTypeV2::Button,
        .actionValue = "true",
    });

    auto setter = reinterpret_cast<bool (*)(std::string_view, std::string_view)>(
        dlsym(RTLD_DEFAULT, kSetConfigSchemaJsonSymbol));
    if (setter == nullptr) {
      return;
    }
    mSchemaPublished = setter(kModuleId, builder.toJson());
  }

  // -------------------------------------------------------------------------
  // Callbacks
  // -------------------------------------------------------------------------
  void handleToggle(bool enabled) {
    Config config = snapshotConfig();
    if (config.masterEnabled != enabled) {
      config.masterEnabled = enabled;
      storeConfig(config);
    }
    applyRuntimeSettings();
  }

  void handleConfigChanged(std::string_view key, std::string_view value) {
    Config config = snapshotConfig();

    if (key == kKeyRestore) {
      config = Config();
      storeConfig(config);
      applyRuntimeSettings();
      publishSchema();
      getSelf().getLogger().info("Settings restored to the defaults");
      return;
    }

    if (key == kKeyMaster) {
      config.masterEnabled = parseBool(value, config.masterEnabled);
    } else if (key == kKeyUpscaling) {
      config.upscalingEnabled = parseBool(value, config.upscalingEnabled);
    } else if (key == kKeyMode) {
      const int mode = parseMode(value, config.upscaleMode);
      if (mode != config.upscaleMode) {
        config.upscaleMode = mode;
        const auto preset = clampMode(mode);
        config.renderScale = scaleForMode(preset);
        config.sharpening = sharpeningForMode(preset);
      }
    } else if (key == kKeyScale) {
      config.renderScale = clampScale(parseInt(value, config.renderScale));
    } else if (key == kKeySharpening) {
      config.sharpening = clampSharpening(parseInt(value, config.sharpening));
    } else if (key == kKeyDynamicResolution) {
      config.dynamicResolution = parseBool(value, config.dynamicResolution);
    } else if (key == kKeyTargetFps) {
      config.targetFps = clampFps(parseInt(value, config.targetFps));
    } else if (key == kKeyGpuOptimization) {
      config.gpuOptimization = parseBool(value, config.gpuOptimization);
    } else if (key == kKeyLowLatency) {
      config.lowLatencyMode = parseBool(value, config.lowLatencyMode);
    } else if (key == kKeyInternalResolution) {
      config.internalResolutionControl =
          parseBool(value, config.internalResolutionControl);
    } else if (key == kKeySpatial) {
      config.spatialUpscaling = parseBool(value, config.spatialUpscaling);
    } else if (key == kKeyDetailRecovery) {
      config.detailRecovery = parseBool(value, config.detailRecovery);
    } else if (key == kKeyFramePacing) {
      config.framePacingOptimization =
          parseBool(value, config.framePacingOptimization);
    } else if (key == kKeyStabilization) {
      config.resolutionStabilization =
          parseBool(value, config.resolutionStabilization);
    } else if (key == kKeyResources) {
      config.resourceManagement = parseBool(value, config.resourceManagement);
    } else if (key == kKeyDamageSwap) {
      config.damageSwap = parseBool(value, config.damageSwap);
    } else if (key == kKeyMinScale) {
      config.minRenderScale = clampScale(parseInt(value, config.minRenderScale));
    } else if (key == kKeyMaxScale) {
      config.maxRenderScale = clampScale(parseInt(value, config.maxRenderScale));
    } else if (key == kKeyOverlay) {
      config.debugOverlay = parseBool(value, config.debugOverlay);
    } else if (key == kKeyGpuTimings) {
      config.gpuTimings = parseBool(value, config.gpuTimings);
    } else if (key == kKeyVerbose) {
      config.verboseLogging = parseBool(value, config.verboseLogging);
    } else if (key == kKeySafeMode) {
      config.safeMode = parseBool(value, config.safeMode);
    } else {
      return;
    }

    storeConfig(config);
    applyRuntimeSettings();
    publishSchema();
  }

  // -------------------------------------------------------------------------
  // Helpers
  // -------------------------------------------------------------------------
  Config snapshotConfig() {
    std::lock_guard<std::mutex> lock(mMutex);
    return mConfig;
  }

  void storeConfig(const Config &config) {
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mConfig = config;
      if (mConfigFile) {
        mConfigFile->save();
      }
    }
  }

  void applyRuntimeSettings() {
    const Config config = snapshotConfig();
    RuntimeSettings settings = toRuntime(config);

    if (config.safeMode) {
      // Safe mode keeps every measurement but stops all rendering changes.
      settings.upscalingEnabled = false;
      settings.internalResolutionControl = false;
      settings.detailRecovery = false;
      settings.sharpening = 0;
      settings.renderScale = 100;
    }

    RenderPipeline::instance().applySettings(settings);
  }

  static std::string describeStatus(const Metrics &metrics,
                                    const gfx::GpuInfo &gpu) {
    std::string status = metrics.status;
    if (!metrics.fallbackReason.empty()) {
      status += " (" + metrics.fallbackReason + ")";
    }
    if (!gpu.renderer.empty()) {
      status += " on " + gpu.renderer;
    }
    return status;
  }

  void onFrame() {
    const Config config = snapshotConfig();
    if (!config.debugOverlay) {
      if (mLastOverlayUpdate.exchange(0) != 0) {
        pl::modmenu::submitDrawCommands(kModuleId, {});
      }
      return;
    }

    const uint64_t now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    const uint64_t last = mLastOverlayUpdate.load();
    if (last != 0 && now - last < 250) {
      return;
    }
    mLastOverlayUpdate.store(now);

    const Metrics metrics = RenderPipeline::instance().metrics();
    const auto &gpu = RenderPipeline::instance().gpuInfo();

    char line1[192];
    char line2[192];
    char line3[192];
    char line4[192];
    char line5[192];
    std::snprintf(line1, sizeof(line1), "LeviFSR AI  %.0f FPS  %.1f ms",
                  metrics.fps, metrics.frameTimeMs);
    std::snprintf(line2, sizeof(line2), "Path: %s",
                  metrics.status.empty() ? "unknown" : metrics.status.c_str());
    std::snprintf(line3, sizeof(line3), "Scale: %d%% (target %d FPS)",
                  metrics.effectiveScale, metrics.targetFps);
    std::snprintf(line4, sizeof(line4), "GPU: %s | %s | AI: %s",
                  gpu.familyLabel().c_str(), gpu.backendLabel().c_str(),
                  mAiStatus.active ? "active" : "inactive");

    // The GPU time is only shown when a real measurement exists. An
    // unavailable measurement is reported as such instead of being estimated.
    if (metrics.gpuTimingValid) {
      std::snprintf(line5, sizeof(line5), "Reconstruction GPU: %.2f ms",
                    metrics.gpuTimeMs);
    } else {
      std::snprintf(line5, sizeof(line5), "Reconstruction GPU: n/a");
    }

    // If no swap was ever intercepted the module cannot do anything. Saying
    // so is better than showing a pipeline that silently never runs.
    if (metrics.swapsObserved == 0) {
      std::snprintf(line5, sizeof(line5),
                    "No swap intercepted: presentation hook inactive");
    }

    std::vector<pl::modmenu::DrawCommand> commands;
    commands.reserve(5);
    const float y = 12.0f;
    commands.push_back({.type = pl::modmenu::DrawCommandType::Text,
                        .x = 12.0f,
                        .y = y,
                        .w = 0.0f,
                        .h = 0.0f,
                        .color = 0xFF4AE0A0U,
                        .size = 20.0f,
                        .text = line1});
    commands.push_back({.type = pl::modmenu::DrawCommandType::Text,
                        .x = 12.0f,
                        .y = y + 26.0f,
                        .color = 0xFFFFFFFFU,
                        .size = 18.0f,
                        .text = line2});
    commands.push_back({.type = pl::modmenu::DrawCommandType::Text,
                        .x = 12.0f,
                        .y = y + 50.0f,
                        .color = 0xFFFFFFFFU,
                        .size = 18.0f,
                        .text = line3});
    commands.push_back({.type = pl::modmenu::DrawCommandType::Text,
                        .x = 12.0f,
                        .y = y + 74.0f,
                        .color = 0xFFFFFFFFU,
                        .size = 18.0f,
                        .text = line4});
    commands.push_back({.type = pl::modmenu::DrawCommandType::Text,
                        .x = 12.0f,
                        .y = y + 98.0f,
                        .color = 0xFFFFFFFFU,
                        .size = 18.0f,
                        .text = line5});
    pl::modmenu::submitDrawCommands(kModuleId, commands);
  }
};

} // namespace

PL_REGISTER_MOD(LeviFsrMod, LeviFsrMod::instance())
