// LeviFSR AI - configuration model.
//
// The configuration is a plain aggregate so that it can be reflected by
// pl::config (boost::pfr) into config/config.json and config/config.schema.json.
// Every value here is consumed by real code; nothing in this file is cosmetic.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace levifsr {

// Upscaling presets. The numeric values are persisted in the config file and
// are also used as the index of the Mod Menu radio entry.
enum class UpscaleMode : int {
  Native = 0,          // No reconstruction, native rendering only.
  Quality = 1,         // 90% internal scale, maximum reconstruction effort.
  Balanced = 2,        // 80% internal scale, balanced effort (default).
  Performance = 3,     // 70% internal scale.
  UltraPerformance = 4 // 60% internal scale, minimum per-frame overhead.
};

// Graphics backend the game is actually running on.
enum class GraphicsBackend : int {
  Unknown = 0,
  OpenGLES = 1,
  Vulkan = 2,
};

// GPU vendor family, derived from the runtime GL_RENDERER / driver strings.
enum class GpuFamily : int {
  Unknown = 0,
  Mali = 1,
  Adreno = 2,
  PowerVr = 3,
  Intel = 4,
  Other = 5,
};

// Which rendering path is currently active. Reported verbatim in diagnostics.
enum class ActivePath : int {
  Disabled = 0,          // Module is off.
  Native = 1,            // No interception, the game renders untouched.
  Reconstruction = 2,    // Presentation-time reconstruction / sharpening only.
  InternalResolution = 3 // Reduced internal resolution + reconstruction.
};

// The scale (in percent) each preset maps to. Used when the user switches the
// upscaling mode; the render-scale control can still override it afterwards.
constexpr int scaleForMode(UpscaleMode mode) {
  switch (mode) {
  case UpscaleMode::Native:
    return 100;
  case UpscaleMode::Quality:
    return 90;
  case UpscaleMode::Balanced:
    return 80;
  case UpscaleMode::Performance:
    return 70;
  case UpscaleMode::UltraPerformance:
    return 60;
  }
  return 80;
}

// Sharpen amount each preset maps to (0 disables the adaptive sharpening).
constexpr int sharpeningForMode(UpscaleMode mode) {
  switch (mode) {
  case UpscaleMode::Native:
    return 0;
  case UpscaleMode::Quality:
    return 25;
  case UpscaleMode::Balanced:
    return 30;
  case UpscaleMode::Performance:
    return 35;
  case UpscaleMode::UltraPerformance:
    return 40;
  }
  return 30;
}

struct Config {
  int version = 1;

  // ---- Main controls -----------------------------------------------------
  bool masterEnabled = true;
  bool upscalingEnabled = true;

  // The defaults are chosen for maximum practical frame rate on mobile GPUs
  // while keeping the world readable: the Performance preset renders the 3D
  // scene at 70% and keeps the interface at the native resolution.
  int upscaleMode = static_cast<int>(UpscaleMode::Performance);
  int renderScale = scaleForMode(UpscaleMode::Performance);
  int sharpening = sharpeningForMode(UpscaleMode::Performance);
  bool dynamicResolution = true;
  int targetFps = 60;
  bool gpuOptimization = true;
  bool lowLatencyMode = true;

  // ---- Advanced ----------------------------------------------------------
  bool internalResolutionControl = true;
  bool spatialUpscaling = true;
  bool detailRecovery = true;
  bool framePacingOptimization = true;
  bool resolutionStabilization = true;
  bool resourceManagement = true;
  bool damageSwap = true;
  bool debugOverlay = false;
  bool gpuTimings = false;
  bool verboseLogging = false;
  bool safeMode = false;
  int minRenderScale = 50;
  int maxRenderScale = 100;
};

namespace limits {
inline constexpr int kMinRenderScale = 50;
inline constexpr int kMaxRenderScale = 100;
inline constexpr int kMinSharpening = 0;
inline constexpr int kMaxSharpening = 100;
inline constexpr int kMinFps = 30;
inline constexpr int kMaxFps = 240;
inline constexpr int kScaleStep = 5;
} // namespace limits

inline const char *modeLabel(UpscaleMode mode) {
  switch (mode) {
  case UpscaleMode::Native:
    return "Native Resolution";
  case UpscaleMode::Quality:
    return "Quality";
  case UpscaleMode::Balanced:
    return "Balanced";
  case UpscaleMode::Performance:
    return "Performance";
  case UpscaleMode::UltraPerformance:
    return "Ultra Performance";
  }
  return "Unknown";
}

inline UpscaleMode clampMode(int raw) {
  if (raw < static_cast<int>(UpscaleMode::Native) ||
      raw > static_cast<int>(UpscaleMode::UltraPerformance)) {
    return UpscaleMode::Balanced;
  }
  return static_cast<UpscaleMode>(raw);
}

inline int clampScale(int raw, int low = limits::kMinRenderScale,
                      int high = limits::kMaxRenderScale) {
  return std::clamp(raw, low, high);
}

inline int clampSharpening(int raw) {
  return std::clamp(raw, limits::kMinSharpening, limits::kMaxSharpening);
}

inline int clampFps(int raw) {
  return std::clamp(raw, limits::kMinFps, limits::kMaxFps);
}

// Brings a freshly loaded (or user edited) configuration into a valid state.
// Returns true when something had to be corrected.
inline bool normalize(Config &config) {
  const Config defaults;
  bool changed = false;

  if (config.version != defaults.version) {
    config.version = defaults.version;
    changed = true;
  }

  const auto mode = clampMode(config.upscaleMode);
  if (mode != static_cast<UpscaleMode>(config.upscaleMode)) {
    config.upscaleMode = static_cast<int>(mode);
    changed = true;
  }

  config.renderScale = clampScale(config.renderScale);
  config.sharpening = clampSharpening(config.sharpening);
  config.targetFps = clampFps(config.targetFps);
  config.minRenderScale = clampScale(config.minRenderScale);
  config.maxRenderScale = clampScale(config.maxRenderScale);

  if (config.minRenderScale > config.maxRenderScale) {
    std::swap(config.minRenderScale, config.maxRenderScale);
    changed = true;
  }
  if (config.renderScale < config.minRenderScale) {
    config.renderScale = config.minRenderScale;
    changed = true;
  }
  if (config.renderScale > config.maxRenderScale) {
    config.renderScale = config.maxRenderScale;
    changed = true;
  }

  // Native mode can never use a reduced internal resolution.
  if (static_cast<UpscaleMode>(config.upscaleMode) == UpscaleMode::Native) {
    if (config.renderScale != 100) {
      config.renderScale = 100;
      changed = true;
    }
  }
  return changed;
}

// Runtime view of the configuration, resolved against the detected hardware
// capabilities. The render pipeline only ever sees this struct.
struct RuntimeSettings {
  bool masterEnabled = false;
  bool upscalingEnabled = false;
  UpscaleMode mode = UpscaleMode::Native;
  int renderScale = 100;
  int sharpening = 0;
  bool dynamicResolution = false;
  int targetFps = 60;
  bool gpuOptimization = false;
  bool lowLatencyMode = false;
  bool internalResolutionControl = false;
  bool spatialUpscaling = false;
  bool detailRecovery = false;
  bool framePacingOptimization = false;
  bool resolutionStabilization = false;
  bool resourceManagement = false;
  bool damageSwap = false;
  bool debugOverlay = false;
  bool gpuTimings = false;
  bool verboseLogging = false;
  bool safeMode = false;
  int minRenderScale = 50;
  int maxRenderScale = 100;
};

inline RuntimeSettings toRuntime(const Config &config) {
  RuntimeSettings settings;
  settings.masterEnabled = config.masterEnabled;
  settings.upscalingEnabled = config.upscalingEnabled;
  settings.mode = clampMode(config.upscaleMode);
  settings.renderScale = config.renderScale;
  settings.sharpening = config.sharpening;
  settings.dynamicResolution = config.dynamicResolution;
  settings.targetFps = config.targetFps;
  settings.gpuOptimization = config.gpuOptimization;
  settings.lowLatencyMode = config.lowLatencyMode;
  settings.internalResolutionControl = config.internalResolutionControl;
  settings.spatialUpscaling = config.spatialUpscaling;
  settings.detailRecovery = config.detailRecovery;
  settings.framePacingOptimization = config.framePacingOptimization;
  settings.resolutionStabilization = config.resolutionStabilization;
  settings.resourceManagement = config.resourceManagement;
  settings.damageSwap = config.damageSwap;
  settings.debugOverlay = config.debugOverlay;
  settings.gpuTimings = config.gpuTimings;
  settings.verboseLogging = config.verboseLogging;
  settings.safeMode = config.safeMode;
  settings.minRenderScale = config.minRenderScale;
  settings.maxRenderScale = config.maxRenderScale;
  return settings;
}

} // namespace levifsr
