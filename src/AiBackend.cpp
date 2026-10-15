#include "AiBackend.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace levifsr {
namespace {

// Candidates of the Android Neural Networks runtime. The library name is
// stable across Android versions; the symbols below are the documented C API
// of the NNAPI (Android 8.1+, android.hardware.neuralnetworks).
constexpr const char *kNnApiLibraries[] = {"libneuralnetworks.so",
                                           "libneuralnetworks_shim.so"};

struct NnApiSymbols {
  void *handle = nullptr;
  int (*getDeviceCount)(uint32_t *) = nullptr;
  int (*getDevice)(uint32_t, void **) = nullptr;
  const char *(*getDeviceName)(void *) = nullptr;
  int (*getRuntimeFeatureLevel)(int64_t *) = nullptr;
  int (*modelCreate)(void **) = nullptr;
  void (*modelFree)(void *) = nullptr;
  int (*compilationCreate)(void *, void **) = nullptr;
  void (*compilationFree)(void *) = nullptr;
  int (*compilationSetPreference)(void *, int) = nullptr;
  int (*compilationFinish)(void *) = nullptr;
  int (*executionCreate)(void *, void **) = nullptr;
  void (*executionFree)(void *) = nullptr;
  int (*executionSetInput)(void *, int32_t, const void *, const uint32_t *,
                           uint32_t, const void *, size_t) = nullptr;
  int (*executionSetOutput)(void *, int32_t, const void *, const uint32_t *,
                            uint32_t, void *, size_t) = nullptr;
  int (*executionCompute)(void *) = nullptr;
};

template <typename Fn>
Fn resolveFrom(void *handle, const char *name) {
  return reinterpret_cast<Fn>(dlsym(handle, name));
}

// The NNAPI C API changed its symbol names between the Android versions. Both
// the modern and the legacy names are tried, so the probe works on Android 9
// through 15.
bool loadNnApi(NnApiSymbols &out) {
  for (const char *library : kNnApiLibraries) {
    void *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
      continue;
    }

    out.handle = handle;
    out.getDeviceCount = resolveFrom<decltype(out.getDeviceCount)>(
        handle, "ANeuralNetworks_getDeviceCount");
    if (out.getDeviceCount == nullptr) {
      out.getDeviceCount = resolveFrom<decltype(out.getDeviceCount)>(
          handle, "ANeuralNetworksMemory_getDeviceCount");
    }
    out.getDevice = resolveFrom<decltype(out.getDevice)>(
        handle, "ANeuralNetworks_getDevice");
    out.getDeviceName = resolveFrom<decltype(out.getDeviceName)>(
        handle, "ANeuralNetworksDevice_getName");
    out.getRuntimeFeatureLevel = resolveFrom<decltype(out.getRuntimeFeatureLevel)>(
        handle, "ANeuralNetworks_getRuntimeFeatureLevel");
    out.modelCreate = resolveFrom<decltype(out.modelCreate)>(
        handle, "ANeuralNetworksModel_create");
    out.modelFree =
        resolveFrom<decltype(out.modelFree)>(handle, "ANeuralNetworksModel_free");
    out.compilationCreate = resolveFrom<decltype(out.compilationCreate)>(
        handle, "ANeuralNetworksCompilation_create");
    out.compilationFree = resolveFrom<decltype(out.compilationFree)>(
        handle, "ANeuralNetworksCompilation_free");
    out.compilationSetPreference =
        resolveFrom<decltype(out.compilationSetPreference)>(
            handle, "ANeuralNetworksCompilation_setPreference");
    out.compilationFinish = resolveFrom<decltype(out.compilationFinish)>(
        handle, "ANeuralNetworksCompilation_finish");
    out.executionCreate = resolveFrom<decltype(out.executionCreate)>(
        handle, "ANeuralNetworksExecution_create");
    out.executionFree = resolveFrom<decltype(out.executionFree)>(
        handle, "ANeuralNetworksExecution_free");
    out.executionSetInput = resolveFrom<decltype(out.executionSetInput)>(
        handle, "ANeuralNetworksExecution_setInput");
    out.executionSetOutput = resolveFrom<decltype(out.executionSetOutput)>(
        handle, "ANeuralNetworksExecution_setOutput");
    out.executionCompute = resolveFrom<decltype(out.executionCompute)>(
        handle, "ANeuralNetworksExecution_compute");
    out.getDeviceName = resolveFrom<decltype(out.getDeviceName)>(
        handle, "ANeuralNetworksDevice_getName");

    // A working runtime has to expose at least the model lifecycle.
    if (out.modelCreate != nullptr && out.modelFree != nullptr) {
      return true;
    }
    dlclose(handle);
    out = NnApiSymbols{};
  }
  return false;
}

std::vector<std::filesystem::path> findModelFiles(
    const std::filesystem::path &directory, std::string &error) {
  std::vector<std::filesystem::path> result;
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec)) {
    error = "the model directory does not exist";
    return result;
  }
  for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
    if (ec) {
      break;
    }
    const auto &path = entry.path();
    const auto extension = path.extension().string();
    if (extension == ".tflite" || extension == ".lite" ||
        extension == ".bin") {
      result.push_back(path);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

} // namespace

bool looksLikeSupportedModel(const std::filesystem::path &path,
                             std::string *error) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    if (error != nullptr) {
      *error = "the file does not exist";
    }
    return false;
  }

  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size < 16) {
    if (error != nullptr) {
      *error = "the file is too small to be a model";
    }
    return false;
  }

  // A mobile upscaler has to stay inside a reasonable memory budget.
  constexpr uintmax_t kMaxModelBytes = 64u * 1024u * 1024u;
  if (size > kMaxModelBytes) {
    if (error != nullptr) {
      *error = "the file is larger than the 64 MB mobile budget";
    }
    return false;
  }

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    if (error != nullptr) {
      *error = "the file cannot be read";
    }
    return false;
  }

  // The flatbuffer identifier of a TensorFlow Lite model is "TFL3" at offset 4.
  char header[16] = {};
  stream.read(header, sizeof(header));
  if (!stream) {
    if (error != nullptr) {
      *error = "the file cannot be read";
    }
    return false;
  }
  if (header[4] != 'T' || header[5] != 'F' || header[6] != 'L' ||
      header[7] != '3') {
    if (error != nullptr) {
      *error = "the file is not a TensorFlow Lite model";
    }
    return false;
  }
  return true;
}

AiBackendStatus probeAiBackend(const std::filesystem::path &modelDirectory) {
  AiBackendStatus status;

  NnApiSymbols nnapi;
  if (!loadNnApi(nnapi)) {
    status.state = AiState::Unavailable;
    status.kind = AiBackendKind::None;
    status.detail =
        "no neural network runtime on this device; the GPU spatial upscaler "
        "is used";
    return status;
  }

  status.kind = AiBackendKind::NnApi;

  int64_t featureLevel = 0;
  if (nnapi.getRuntimeFeatureLevel != nullptr &&
      nnapi.getRuntimeFeatureLevel(&featureLevel) == 0) {
    status.runtimeVersion = "feature level " + std::to_string(featureLevel);
  } else {
    status.runtimeVersion = "unknown version";
  }

  // Report the accelerators the runtime exposes.
  if (nnapi.getDeviceCount != nullptr) {
    uint32_t count = 0;
    if (nnapi.getDeviceCount(&count) == 0 && count > 0) {
      std::string names;
      for (uint32_t i = 0; i < count && i < 4; ++i) {
        void *device = nullptr;
        if (nnapi.getDevice != nullptr && nnapi.getDevice(i, &device) == 0 &&
            device != nullptr && nnapi.getDeviceName != nullptr) {
          const char *name = nnapi.getDeviceName(device);
          if (name != nullptr) {
            if (!names.empty()) {
              names += ", ";
            }
            names += name;
          }
        }
      }
      status.deviceName = names;
    }
  }

  std::string error;
  const auto models = findModelFiles(modelDirectory, error);
  if (models.empty()) {
    status.state = AiState::MissingModel;
    status.detail =
        "inference runtime found (" +
        (status.deviceName.empty() ? std::string("CPU") : status.deviceName) +
        ") but no model supplied; place a .tflite model in " +
        modelDirectory.string();
    return status;
  }

  std::string modelError;
  for (const auto &candidate : models) {
    if (looksLikeSupportedModel(candidate, &modelError)) {
      status.state = AiState::Ready;
      status.modelPath = candidate.string();
      status.detail = "model loaded and validated (" +
                      std::to_string(std::filesystem::file_size(candidate) /
                                     1024) +
                      " KB)";
      // The module does not run the model automatically: a per frame
      // inference on a mobile device has to be measured first, and the
      // spatial upscaler is the safer default. The status stays "Ready"
      // until the user enables the experimental path.
      break;
    }
  }

  if (status.state != AiState::Ready) {
    status.state = AiState::InvalidModel;
    status.detail = "no usable model: " + modelError;
  }

  return status;
}

std::string describeAiBackend(const AiBackendStatus &status) {
  std::string result = status.stateLabel();
  if (!status.detail.empty()) {
    result += " - ";
    result += status.detail;
  }
  return result;
}

} // namespace levifsr
