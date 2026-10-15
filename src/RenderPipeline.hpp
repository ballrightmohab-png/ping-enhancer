// LeviFSR AI - rendering pipeline.
//
// The module works at two levels:
//
//  1. Presentation level (always available when the game renders through
//     EGL/OpenGL ES): the swap is intercepted, the presented frame is copied
//     into a module owned texture on the GPU (no CPU readback) and redrawn
//     with the reconstruction shader.
//
//  2. Internal resolution level (requires the game to render the 3D scene into
//     an offscreen target): that target is detected at runtime and allocated
//     at a reduced size, so the expensive fragment work really happens at the
//     lower resolution while the game keeps drawing the interface on the
//     native surface. This is genuine reduced-resolution rendering, not a
//     post-process filter.
//
// Everything degrades gracefully: when a level is not available the module
// reports the reason and keeps the game rendering untouched.

#pragma once

#include "Config.hpp"
#include "DynamicResolution.hpp"
#include "GlApi.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace levifsr {

// ---------------------------------------------------------------------------
// Scene target scaler: reduces the internal render resolution of the game.
// ---------------------------------------------------------------------------
class SceneTargetScaler {
public:
  struct State {
    bool detected = false;
    bool applied = false;
    bool verified = false;
    uint32_t framebuffer = 0;
    uint32_t colorTexture = 0;
    uint32_t depthRenderbuffer = 0;
    int originalWidth = 0;
    int originalHeight = 0;
    int scaledWidth = 0;
    int scaledHeight = 0;
    std::string reason;
  };

  void configure(const gfx::Api *api, bool enabled, int targetScalePercent);
  void setDisplaySize(int width, int height);
  void onFrameEnd();
  void reset();

  [[nodiscard]] State state() const;
  [[nodiscard]] bool usable() const { return mUsable; }

  // Entry points used by the GL trampolines.
  void noteFramebufferBind(uint32_t framebuffer);
  void noteViewport(int x, int y, int width, int height);
  void noteRenderbufferStorage(uint32_t internalFormat, int width, int height,
                               int samples);
  void noteBindRenderbuffer(uint32_t renderbuffer);
  void noteBindTexture(uint32_t target, uint32_t texture);
  void noteTexImage(uint32_t internalFormat, int level, int width, int height);
  void noteTexStorage(uint32_t internalFormat, int levels, int width,
                      int height);
  void noteDeleteTextures(const uint32_t *textures, int count);
  void noteFramebufferTexture(uint32_t framebuffer, uint32_t attachment,
                               uint32_t texture);
  void noteFramebufferRenderbuffer(uint32_t framebuffer, uint32_t attachment,
                                   uint32_t renderbuffer);

  // Clamps a viewport to the reduced scene target, called from the viewport
  // trampoline before the value is forwarded to the game.
  void clampViewport(int &x, int &y, int &width, int &height);

private:
  struct RenderbufferInfo {
    uint32_t id = 0;
    uint32_t internalFormat = 0;
    int width = 0;
    int height = 0;
    int samples = 0;
  };

  struct TextureAttachment {
    uint32_t framebuffer = 0;
    uint32_t attachment = 0;
    uint32_t texture = 0;
  };

  // Parameters of a texture the module learned from the intercepted calls.
  // OpenGL ES 3 has no API to query the size or the internal format of a
  // texture, so the module has to track them.
  struct TextureInfo {
    uint32_t id = 0;
    uint32_t internalFormat = 0;
    int width = 0;
    int height = 0;
    int levels = 1;
    bool immutable = false;
  };

  struct RenderbufferAttachment {
    uint32_t framebuffer = 0;
    uint32_t attachment = 0;
    uint32_t renderbuffer = 0;
  };

  [[nodiscard]] const RenderbufferInfo *findRenderbuffer(uint32_t id) const;
  [[nodiscard]] const TextureInfo *findTexture(uint32_t id) const;
  [[nodiscard]] const TextureAttachment *
  findTextureAttachment(uint32_t framebuffer, uint32_t attachment) const;
  [[nodiscard]] const RenderbufferAttachment *findRenderbufferAttachment(
      uint32_t framebuffer, uint32_t attachment) const;

  bool applyScale(int percent);
  bool resizeTexture(uint32_t texture, uint32_t internalFormat, int width,
                     int height);
  void restoreOriginalSize();
  void releaseTracking();

  const gfx::Api *mApi = nullptr;
  bool mEnabled = false;
  bool mUsable = false;
  int mTargetScale = 100;
  int mDisplayWidth = 0;
  int mDisplayHeight = 0;

  uint32_t mBoundFramebuffer = 0;
  uint32_t mBoundRenderbuffer = 0;
  uint32_t mBoundTexture2D = 0;
  int mViewportWidth = 0;
  int mViewportHeight = 0;
  uint32_t mCandidateFramebuffer = 0;

  std::vector<RenderbufferInfo> mRenderbuffers;
  std::vector<TextureInfo> mTextures;
  std::vector<TextureAttachment> mTextureAttachments;
  std::vector<RenderbufferAttachment> mRenderbufferAttachments;
  mutable std::mutex mStateMutex;
  State mState;
};

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
struct Metrics {
  double fps = 0.0;
  double frameTimeMs = 0.0;
  double gpuTimeMs = 0.0;
  bool gpuTimingValid = false;
  double reconstructionCostMs = 0.0;
  uint64_t frames = 0;
  uint64_t swapsObserved = 0;
  int surfaceWidth = 0;
  int surfaceHeight = 0;
  int sceneWidth = 0;
  int sceneHeight = 0;
  int effectiveScale = 100;
  int targetFps = 60;
  ActivePath path = ActivePath::Disabled;
  bool dynamicResolutionActive = false;
  bool internalResolutionActive = false;
  bool reconstructionActive = false;
  std::string status;
  std::string fallbackReason;
};

// ---------------------------------------------------------------------------
// Render pipeline
// ---------------------------------------------------------------------------
class RenderPipeline {
public:
  static RenderPipeline &instance();

  // Installs the presentation hook. Returns false when the graphics stack
  // does not allow interception (for example a Vulkan only game).
  bool install();
  void uninstall();

  void applySettings(const RuntimeSettings &settings);
  void setFrameCallback(std::function<void()> callback);
  [[nodiscard]] Metrics metrics() const;
  [[nodiscard]] const gfx::GpuInfo &gpuInfo() const { return mGpuInfo; }
  [[nodiscard]] ActivePath activePath() const { return mPath; }
  [[nodiscard]] std::string statusLine() const;
  [[nodiscard]] bool renderingActive() const { return mPath != ActivePath::Disabled; }
  [[nodiscard]] SceneTargetScaler &scaler() { return mScaler; }

  // Called by the module when the surface size changed (rotation, resize).
  void notifySurfaceResized();

private:
  RenderPipeline() = default;

  static EGLBoolean swapTrampoline(EGLDisplay display, EGLSurface surface);
  void onPresent(EGLDisplay display, EGLSurface surface);

  bool ensureContextResources();
  void releaseContextResources();
  bool ensureProgram();
  void updateSurfaceSize(EGLDisplay display, EGLSurface surface);
  void runReconstructionPass();
  void collectGpuTiming();
  void updateTimings(uint64_t nowNs);

  bool mHooked = false;
  void *mEglHandle = nullptr;
  void *mSwapAddress = nullptr;
  EGLBoolean (*mOriginalSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

  gfx::Api mApi;
  gfx::GpuInfo mGpuInfo;
  EGLContext mContext = EGL_NO_CONTEXT;
  bool mContextReady = false;

  uint32_t mCaptureTexture = 0;
  uint32_t mCaptureFramebuffer = 0;
  uint32_t mVertexArray = 0;
  uint32_t mProgram = 0;
  int mUniformSource = -1;
  int mUniformDetail = -1;
  int mUniformOutputSize = -1;
  int mUniformSourceSize = -1;
  int mUniformDetailSize = -1;
  int mUniformSharpness = -1;
  int mUniformDetailStrength = -1;
  int mUniformUseDetail = -1;
  std::string mProgramLog;

  RuntimeSettings mSettings;
  RuntimeSettings mPendingSettings;
  std::atomic<bool> mSettingsDirty{true};
  std::mutex mSettingsMutex;

  ActivePath mPath = ActivePath::Disabled;
  std::string mFallbackReason;
  mutable std::mutex mStatusMutex;

  DynamicResolutionController mDynamicResolution;
  SceneTargetScaler mScaler;

  uint64_t mLastFrameNs = 0;
  std::atomic<uint64_t> mFrameCounter{0};
  std::atomic<uint64_t> mSwapCounter{0};
  std::atomic<double> mFrameTimeMs{0.0};
  std::atomic<double> mFps{0.0};
  std::atomic<double> mReconstructionCostMs{0.0};
  std::atomic<int> mSurfaceWidth{0};
  std::atomic<int> mSurfaceHeight{0};
  std::atomic<bool> mSurfaceKnown{false};

  // Optional GPU timing (GL_EXT_disjoint_timer_query).
  bool mTimerQuerySupported = false;
  uint32_t mTimerQuery = 0;
  bool mTimerQueryActive = false;
  int mTimerQueriesPending = 0;
  std::atomic<double> mGpuTimeMs{0.0};
  std::atomic<bool> mGpuTimingValid{false};

  std::atomic<bool> mShutdownRequested{false};
  std::mutex mFrameCallbackMutex;
  std::function<void()> mFrameCallback;
};

} // namespace levifsr
