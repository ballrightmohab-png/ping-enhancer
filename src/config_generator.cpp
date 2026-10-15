// LeviFSR AI - host side configuration generator.
//
// Produces the config/config.json and config/config.schema.json that ship
// inside the .levipack. This mirrors examples/full-cpp-mod: the C++ Config
// struct is the single source of truth, and the JSON is generated from it so
// the packaged defaults can never drift from the compiled ones.

#include "Config.hpp"

#include <pl/Config.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool writeJson(const std::filesystem::path &path,
               const nlohmann::json &value) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    std::cerr << "Failed to create " << path.parent_path() << ": "
              << ec.message() << '\n';
    return false;
  }

  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    std::cerr << "Failed to open " << path << '\n';
    return false;
  }
  stream << value.dump(2) << '\n';
  return stream.good();
}

} // namespace

// Metadata shown by the launcher's configuration editor.
template <> struct pl::config::Schema<levifsr::Config> {
  static constexpr std::string_view title = "LeviFSR AI";
  static constexpr std::string_view description =
      "AI-Powered Upscaling & Low-Latency Rendering for Minecraft Bedrock.";

  static constexpr pl::config::FieldSchema field(std::string_view name) {
    using namespace std::string_view_literals;
    if (name == "version") {
      return {.title = "Config Version", .readOnly = true};
    }
    if (name == "masterEnabled") {
      return {.title = "Master Enable",
              .description = "Turns the whole module on or off."};
    }
    if (name == "upscalingEnabled") {
      return {.title = "Upscaling",
              .description = "Enables the reconstruction pass."};
    }
    if (name == "upscaleMode") {
      return {.title = "Upscaling Mode",
              .description = "Reconstruction preset."};
    }
    if (name == "renderScale") {
      return {.title = "Render Scale",
              .description = "Internal 3D render resolution in percent.",
              .minimum = 50, .maximum = 100};
    }
    if (name == "sharpening") {
      return {.title = "Sharpening",
              .description = "Contrast adaptive sharpening strength.",
              .minimum = 0, .maximum = 100};
    }
    if (name == "dynamicResolution") {
      return {.title = "Dynamic Resolution",
              .description = "Adapts the render scale to hold the FPS target."};
    }
    if (name == "targetFps") {
      return {.title = "Target FPS",
              .description = "Frame rate the adaptive controller aims for.",
              .minimum = 30, .maximum = 240};
    }
    if (name == "gpuOptimization") {
      return {.title = "GPU Optimization",
              .description = "Vendor aware tuning for Mali and Adreno."};
    }
    if (name == "lowLatencyMode") {
      return {.title = "Low-Latency Mode",
              .description = "Keeps the frame path free of added latency."};
    }
    if (name == "internalResolutionControl") {
      return {.title = "Internal Resolution Control",
              .description = "Reduces the size of the game's 3D render "
                             "target."};
    }
    if (name == "spatialUpscaling") {
      return {.title = "Spatial Upscaling",
              .description = "Edge adaptive spatial reconstruction."};
    }
    if (name == "detailRecovery") {
      return {.title = "Detail Recovery",
              .description = "Rebuilds high frequency detail lost to the "
                             "upscale."};
    }
    if (name == "framePacingOptimization") {
      return {.title = "Frame Pacing Optimization"};
    }
    if (name == "resolutionStabilization") {
      return {.title = "Render Resolution Stabilization"};
    }
    if (name == "resourceManagement") {
      return {.title = "Resource and Buffer Management"};
    }
    if (name == "damageSwap") {
      return {.title = "Damage Based Swap"};
    }
    if (name == "debugOverlay") {
      return {.title = "Diagnostic Overlay"};
    }
    if (name == "gpuTimings") {
      return {.title = "GPU Timings"};
    }
    if (name == "verboseLogging") {
      return {.title = "Verbose Logging"};
    }
    if (name == "safeMode") {
      return {.title = "Safe Mode",
              .description = "Disables every rendering change."};
    }
    if (name == "minRenderScale") {
      return {.title = "Minimum Render Scale", .minimum = 50, .maximum = 100};
    }
    if (name == "maxRenderScale") {
      return {.title = "Maximum Render Scale", .minimum = 50, .maximum = 100};
    }
    return {};
  }
};

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: levifsr_config_generator <output-config-dir>\n";
    return 2;
  }

  const std::filesystem::path outputDir = argv[1];
  const levifsr::Config defaults;

  if (!writeJson(outputDir / "config.json",
                 pl::config::defaultJson(defaults))) {
    return 1;
  }
  if (!writeJson(outputDir / "config.schema.json",
                 pl::config::schema(defaults))) {
    return 1;
  }

  std::cout << "Wrote " << (outputDir / "config.json") << " and "
            << (outputDir / "config.schema.json") << '\n';
  return 0;
}
