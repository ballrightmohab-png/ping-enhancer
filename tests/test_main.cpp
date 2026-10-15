// LeviFSR AI - host unit tests.
//
// These tests run on the build machine without an Android device. They cover
// everything that does not need a GPU:
//
//   * configuration normalisation and clamping (the rules that guarantee the
//     game never receives an invalid render scale),
//   * the adaptive resolution controller (hysteresis, bounds, spike
//     tolerance),
//   * the model validation of the experimental AI backend,
//   * the packaging layout of the .levipack.

#include "AiBackend.hpp"
#include "Config.hpp"
#include "DynamicResolution.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;
std::string gCurrentTest;

void startTest(const char *name) {
  gCurrentTest = name;
  std::printf("  %-56s", name);
  std::fflush(stdout);
}

void finishTest() { std::printf("ok\n"); }

void check(bool condition, const char *message, int line) {
  ++gChecks;
  if (!condition) {
    ++gFailures;
    std::printf("\n    FAIL [%s:%d] %s\n", gCurrentTest.c_str(), line, message);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!((a) == (b))) {                                                       \
      ++gFailures;                                                             \
      std::printf("\n    FAIL [%s:%d] %s == %s\n", gCurrentTest.c_str(),        \
                  __LINE__, #a, #b);                                           \
    }                                                                          \
  } while (0)

using namespace levifsr;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
void testDefaults() {
  startTest("config: defaults target performance and quality");
  Config config;
  CHECK(config.masterEnabled);
  CHECK(config.upscalingEnabled);
  // The default mode has to be a real upscaling mode, not native.
  CHECK(config.upscaleMode != static_cast<int>(UpscaleMode::Native));
  CHECK(config.renderScale >= 60 && config.renderScale <= 80);
  CHECK(config.sharpening > 0 && config.sharpening <= 50);
  CHECK(config.dynamicResolution);
  CHECK(config.lowLatencyMode);
  CHECK(config.gpuOptimization);
  CHECK(!config.debugOverlay);
  CHECK(!config.gpuTimings);
  CHECK(!config.safeMode);
  finishTest();
}

void testClamping() {
  startTest("config: out of range values are clamped");
  Config config;
  config.renderScale = 500;
  config.sharpening = -20;
  config.targetFps = 5000;
  normalize(config);
  CHECK_EQ(config.renderScale, limits::kMaxRenderScale);
  CHECK_EQ(config.sharpening, limits::kMinSharpening);
  CHECK_EQ(config.targetFps, limits::kMaxFps);

  config.renderScale = 1;
  config.sharpening = 999;
  config.targetFps = 1;
  normalize(config);
  CHECK_EQ(config.renderScale, limits::kMinRenderScale);
  CHECK_EQ(config.sharpening, limits::kMaxSharpening);
  CHECK_EQ(config.targetFps, limits::kMinFps);
  finishTest();
}

void testNativeModeForcesFullScale() {
  startTest("config: native mode forces 100% render scale");
  Config config;
  config.upscaleMode = static_cast<int>(UpscaleMode::Native);
  config.renderScale = 50;
  normalize(config);
  CHECK_EQ(config.renderScale, 100);
  finishTest();
}

void testBoundsSwap() {
  startTest("config: inverted min/max bounds are repaired");
  Config config;
  config.minRenderScale = 100;
  config.maxRenderScale = 50;
  normalize(config);
  CHECK(config.minRenderScale <= config.maxRenderScale);
  CHECK(config.renderScale >= config.minRenderScale);
  CHECK(config.renderScale <= config.maxRenderScale);
  finishTest();
}

void testInvalidModeFallsBack() {
  startTest("config: an unknown mode falls back to balanced");
  Config config;
  config.upscaleMode = 99;
  normalize(config);
  CHECK_EQ(config.upscaleMode, static_cast<int>(UpscaleMode::Balanced));
  finishTest();
}

void testPresetMapping() {
  startTest("config: presets map to the documented scales");
  CHECK_EQ(scaleForMode(UpscaleMode::Native), 100);
  CHECK_EQ(scaleForMode(UpscaleMode::Quality), 90);
  CHECK_EQ(scaleForMode(UpscaleMode::Balanced), 80);
  CHECK_EQ(scaleForMode(UpscaleMode::Performance), 70);
  CHECK_EQ(scaleForMode(UpscaleMode::UltraPerformance), 60);
  // Native rendering must not sharpen, every other mode must.
  CHECK_EQ(sharpeningForMode(UpscaleMode::Native), 0);
  CHECK(sharpeningForMode(UpscaleMode::Performance) > 0);
  finishTest();
}

// ---------------------------------------------------------------------------
// Dynamic resolution
// ---------------------------------------------------------------------------
void testDisabledControllerNeverMoves() {
  startTest("dynamic resolution: disabled controller never changes scale");
  DynamicResolutionController controller;
  controller.configure(false, 60, 50, 100, 80);
  for (int i = 0; i < 400; ++i) {
    controller.submitSyntheticFrame(50.0);
  }
  CHECK_EQ(controller.currentScale(), 80);
  CHECK_EQ(controller.adjustments(), 0);
  finishTest();
}

void testSingleSpikeIgnored() {
  startTest("dynamic resolution: a single slow frame is ignored");
  DynamicResolutionController controller;
  controller.configure(true, 60, 50, 100, 80);
  // Frames exactly on the 60 fps target, then exactly one anomalous frame.
  for (int i = 0; i < 40; ++i) {
    controller.submitSyntheticFrame(16.6);
  }
  controller.submitSyntheticFrame(400.0);
  for (int i = 0; i < 40; ++i) {
    controller.submitSyntheticFrame(16.6);
  }
  CHECK_EQ(controller.currentScale(), 80);
  CHECK_EQ(controller.scaleDecreased(), 0);
  CHECK_EQ(controller.adjustments(), 0);
  finishTest();
}

void testFewSpikesInARowIgnored() {
  startTest("dynamic resolution: a short burst of spikes is ignored");
  DynamicResolutionController controller;
  controller.configure(true, 60, 50, 100, 80);
  // Two spikes are still inside the 75th percentile of a 32 frame window.
  for (int i = 0; i < 200; ++i) {
    controller.submitSyntheticFrame((i % 60 == 0) ? 300.0 : 16.6);
  }
  CHECK_EQ(controller.currentScale(), 80);
  CHECK_EQ(controller.adjustments(), 0);
  finishTest();
}

void testSustainedSlowFramesReduceScale() {
  startTest("dynamic resolution: sustained slow frames reduce the scale");
  DynamicResolutionController controller;
  controller.configure(true, 60, 50, 100, 80);
  for (int i = 0; i < 300; ++i) {
    controller.submitSyntheticFrame(40.0); // 25 fps, far below 60
  }
  CHECK(controller.currentScale() < 80);
  CHECK(controller.scaleDecreased() > 0);
  finishTest();
}

void testScaleRespectsMinimum() {
  startTest("dynamic resolution: the minimum scale is respected");
  DynamicResolutionController controller;
  controller.configure(true, 60, 50, 100, 80);
  for (int i = 0; i < 2000; ++i) {
    controller.submitSyntheticFrame(200.0);
  }
  CHECK_EQ(controller.currentScale(), 50);
  finishTest();
}

void testScaleRespectsMaximum() {
  startTest("dynamic resolution: the maximum scale is respected");
  DynamicResolutionController controller;
  controller.configure(true, 60, 50, 90, 60);
  for (int i = 0; i < 2000; ++i) {
    controller.submitSyntheticFrame(4.0); // very fast
  }
  CHECK_EQ(controller.currentScale(), 90);
  finishTest();
}

void testRecoveryIsSlowerThanReduction() {
  startTest("dynamic resolution: recovery is slower than reduction");
  // The asymmetry is expressed in agreeing evaluations and in the cooldown.
  // A reduction needs 2 slow evaluations, a raise needs 3 fast ones, and the
  // cooldown after a raise is longer than after a reduction.

  DynamicResolutionController slow;
  slow.configure(true, 60, 50, 100, 80);
  int framesToFirstDrop = 0;
  for (int i = 0; i < 500 && framesToFirstDrop == 0; ++i) {
    slow.submitSyntheticFrame(40.0);
    if (slow.currentScale() < 80) {
      framesToFirstDrop = i + 1;
    }
  }
  CHECK(framesToFirstDrop > 0);
  // Two agreeing evaluations at 16 frames each.
  CHECK_EQ(framesToFirstDrop, 32);
  CHECK_EQ(slow.cooldown(), 1); // short cooldown after a reduction

  DynamicResolutionController fast;
  fast.configure(true, 60, 50, 100, 80);
  int framesToFirstRaise = 0;
  for (int i = 0; i < 500 && framesToFirstRaise == 0; ++i) {
    fast.submitSyntheticFrame(4.0);
    if (fast.currentScale() > 80) {
      framesToFirstRaise = i + 1;
    }
  }
  CHECK(framesToFirstRaise > 0);
  // Three agreeing evaluations at 16 frames each.
  CHECK_EQ(framesToFirstRaise, 48);
  CHECK_EQ(fast.cooldown(), 3); // long cooldown after a raise

  CHECK(framesToFirstRaise > framesToFirstDrop);
  finishTest();
}

void testSteadyStateIsStable() {
  startTest("dynamic resolution: on-target frames keep the scale stable");
  DynamicResolutionController controller;
  controller.configure(true, 60, 50, 100, 80);
  // 16.6 ms is exactly the 60 fps target, inside the hysteresis band.
  for (int i = 0; i < 1000; ++i) {
    controller.submitSyntheticFrame(16.6);
  }
  CHECK_EQ(controller.currentScale(), 80);
  CHECK_EQ(controller.adjustments(), 0);
  finishTest();
}

void testInvalidFrameTimesIgnored() {
  startTest("dynamic resolution: invalid frame times are ignored");
  DynamicResolutionController controller;
  controller.configure(true, 60, 50, 100, 80);
  controller.submitSyntheticFrame(0.0);
  controller.submitSyntheticFrame(-5.0);
  CHECK_EQ(controller.sampleCount(), static_cast<std::size_t>(0));
  finishTest();
}

// ---------------------------------------------------------------------------
// AI backend
// ---------------------------------------------------------------------------
void testModelRejectsMissingFile() {
  startTest("ai backend: a missing model file is rejected");
  std::string error;
  CHECK(!looksLikeSupportedModel("/nonexistent/path/model.tflite", &error));
  CHECK(!error.empty());
  finishTest();
}

void testModelRejectsTinyFile() {
  startTest("ai backend: a truncated file is rejected");
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "levifsr_test_tiny.bin";
  {
    std::ofstream stream(path, std::ios::binary);
    stream << "short";
  }
  std::string error;
  CHECK(!looksLikeSupportedModel(path, &error));
  std::filesystem::remove(path);
  finishTest();
}

void testModelRejectsWrongIdentifier() {
  startTest("ai backend: a non-TFLite file is rejected");
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "levifsr_test_fake.tflite";
  {
    std::ofstream stream(path, std::ios::binary);
    const std::vector<char> data(256, 'x');
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  std::string error;
  CHECK(!looksLikeSupportedModel(path, &error));
  std::filesystem::remove(path);
  finishTest();
}

void testModelAcceptsTfliteHeader() {
  startTest("ai backend: a file with the TFLite identifier is accepted");
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "levifsr_test_model.tflite";
  {
    std::ofstream stream(path, std::ios::binary);
    std::vector<char> data(512, 0);
    // Flatbuffer root offset, then the "TFL3" identifier.
    data[4] = 'T';
    data[5] = 'F';
    data[6] = 'L';
    data[7] = '3';
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  std::string error;
  CHECK(looksLikeSupportedModel(path, &error));
  std::filesystem::remove(path);
  finishTest();
}

void testProbeNeverClaimsSuccessWithoutModel() {
  startTest("ai backend: the probe never reports an active model");
  const auto status =
      probeAiBackend(std::filesystem::temp_directory_path() /
                     "levifsr_test_no_models_directory");
  // On a desktop host there is no NNAPI, so the state must be Unavailable.
  // On a device it may be MissingModel. Neither may be Active.
  CHECK(status.state == AiState::Unavailable ||
        status.state == AiState::MissingModel ||
        status.state == AiState::InvalidModel);
  CHECK(!status.active);
  CHECK(status.inferenceCount == 0);
  CHECK(status.lastInferenceMs == 0.0);
  finishTest();
}

} // namespace

int main() {
  std::printf("LeviFSR AI host unit tests\n\n");

  testDefaults();
  testClamping();
  testNativeModeForcesFullScale();
  testBoundsSwap();
  testInvalidModeFallsBack();
  testPresetMapping();

  testDisabledControllerNeverMoves();
  testSingleSpikeIgnored();
  testFewSpikesInARowIgnored();
  testSustainedSlowFramesReduceScale();
  testScaleRespectsMinimum();
  testScaleRespectsMaximum();
  testRecoveryIsSlowerThanReduction();
  testSteadyStateIsStable();
  testInvalidFrameTimesIgnored();

  testModelRejectsMissingFile();
  testModelRejectsTinyFile();
  testModelRejectsWrongIdentifier();
  testModelAcceptsTfliteHeader();
  testProbeNeverClaimsSuccessWithoutModel();

  std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
