// LeviFSR AI - experimental machine learning upscaling backend.
//
// This file deliberately contains NO neural network: the module does not ship
// pretrained weights, it does not download anything and it does not pretend
// that a shader is machine learning.
//
// What it does instead is probe, at load time, whether a genuine inference
// path exists on the device, using only interfaces that actually exist on
// Android:
//
//   * the Android Neural Networks runtime (libneuralnetworks.so, Android 8.1
//     and later), reached through dlopen so the module stays independent of
//     any particular NDK version,
//   * a model file that the *user* places in `<mod dir>/models/` themselves,
//     in the standard TFLite flatbuffer layout that the runtime consumes.
//
// The backend reports its real state through AiBackendStatus. That status is
// exactly what the settings menu and the diagnostic overlay display. When no
// model is supplied the state is MissingModel and the module keeps running the
// GPU spatial upscaler, which is the primary and fully functional path.
//
// Nothing here ever reports success unless a model was really parsed, a
// delegate really was obtained, and an inference really did run.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace levifsr {

enum class AiBackendKind {
  None,
  NnApi,        // Android Neural Networks API (CPU / NPU / GPU delegate).
  NotAvailable, // A runtime exists but no usable delegate was found.
};

enum class AiState {
  Unavailable,  // No inference runtime on this device at all.
  MissingModel, // Runtime present, but the user supplied no model.
  InvalidModel, // A model exists but it is not a supported flatbuffer.
  Ready,        // Loaded and validated, not running yet.
  Active,       // Executing during the reconstruction pass.
  Disabled,     // Turned off by the settings or by safe mode.
  Error,        // Initialisation or execution failed.
};

struct AiBackendStatus {
  AiState state = AiState::Unavailable;
  AiBackendKind kind = AiBackendKind::None;

  // True only while the backend really executes an inference per frame.
  bool active = false;

  std::string detail;
  std::string runtimeVersion;
  std::string modelPath;
  std::string deviceName;

  // Model shape, only valid when state is Ready or Active.
  std::vector<int> inputShape;
  std::vector<int> outputShape;

  // Measured inference cost. Zero until an inference actually ran.
  double lastInferenceMs = 0.0;
  uint64_t inferenceCount = 0;

  [[nodiscard]] const char *stateLabel() const {
    switch (state) {
    case AiState::Unavailable:
      return "Unavailable";
    case AiState::MissingModel:
      return "No model";
    case AiState::InvalidModel:
      return "Invalid model";
    case AiState::Ready:
      return "Ready";
    case AiState::Active:
      return "Active";
    case AiState::Disabled:
      return "Disabled";
    case AiState::Error:
      return "Error";
    }
    return "Unknown";
  }
};

// Probes the device for a usable inference runtime and, when the user supplied
// a model, loads and validates it.
//
// The function never throws, never blocks for a long time and never performs
// network access. It is safe to call from the module lifecycle.
AiBackendStatus probeAiBackend(const std::filesystem::path &modelDirectory);

// Reads the first bytes of a file and checks the flatbuffer identifier. This
// is the only validation performed before the runtime itself parses the model:
// a corrupt file is rejected here instead of crashing during execution.
bool looksLikeSupportedModel(const std::filesystem::path &path,
                             std::string *error);

// Human readable summary used by the diagnostics.
std::string describeAiBackend(const AiBackendStatus &status);

} // namespace levifsr
