#include "RenderPipeline.hpp"

#include "Shaders.hpp"

#include <dlfcn.h>

#include <pl/memory/Hook.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>

// GL_EXT_disjoint_timer_query / GL_ARB_timer_query constants. They are not
// part of the GLES3 core header, so they are declared here when the platform
// header does not provide them.
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

namespace levifsr {
namespace {

// ---------------------------------------------------------------------------
// GL trampolines
// ---------------------------------------------------------------------------

using PFNGlBindFramebuffer = void (*)(GLenum, GLuint);
using PFNGlViewport = void (*)(GLint, GLint, GLsizei, GLsizei);
using PFNGlRenderbufferStorage = void (*)(GLenum, GLenum, GLsizei, GLsizei);
using PFNGlRenderbufferStorageMultisample =
    void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using PFNGlBindRenderbuffer = void (*)(GLenum, GLuint);
using PFNGlTexImage2D = void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                                 GLenum, GLenum, const void *);
using PFNGlTexStorage2D = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using PFNGlDeleteTextures = void (*)(GLsizei, const GLuint *);
using PFNGlBindTexture = void (*)(GLenum, GLuint);
using PFNGlFramebufferTexture2D = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFNGlFramebufferRenderbuffer =
    void (*)(GLenum, GLenum, GLenum, GLuint);

PFNGlBindFramebuffer gOriginalBindFramebuffer = nullptr;
PFNGlViewport gOriginalViewport = nullptr;
PFNGlRenderbufferStorage gOriginalRenderbufferStorage = nullptr;
PFNGlRenderbufferStorageMultisample gOriginalRenderbufferStorageMultisample =
    nullptr;
PFNGlBindRenderbuffer gOriginalBindRenderbuffer = nullptr;
PFNGlTexImage2D gOriginalTexImage2D = nullptr;
PFNGlTexStorage2D gOriginalTexStorage2D = nullptr;
PFNGlDeleteTextures gOriginalDeleteTextures = nullptr;
PFNGlBindTexture gOriginalBindTexture = nullptr;
PFNGlFramebufferTexture2D gOriginalFramebufferTexture2D = nullptr;
PFNGlFramebufferRenderbuffer gOriginalFramebufferRenderbuffer = nullptr;

pl::memory::HookHandle gBindFramebufferHook;
pl::memory::HookHandle gViewportHook;
pl::memory::HookHandle gRenderbufferStorageHook;
pl::memory::HookHandle gBindRenderbufferHook;
pl::memory::HookHandle gTexImage2DHook;
pl::memory::HookHandle gTexStorage2DHook;
pl::memory::HookHandle gDeleteTexturesHook;
pl::memory::HookHandle gBindTextureHook;
pl::memory::HookHandle gFramebufferTexture2DHook;
pl::memory::HookHandle gFramebufferRenderbufferHook;

void hookedBindFramebuffer(GLenum target, GLuint framebuffer) {
  if (gOriginalBindFramebuffer != nullptr) {
    gOriginalBindFramebuffer(target, framebuffer);
  }
  RenderPipeline::instance().scaler().noteFramebufferBind(framebuffer);
}

void hookedViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  auto &scaler = RenderPipeline::instance().scaler();
  scaler.noteViewport(x, y, width, height);
  if (gOriginalViewport != nullptr) {
    // The scene target is smaller than the surface, the viewport has to be
    // clamped so the game does not render outside of the reduced target.
    scaler.clampViewport(x, y, width, height);
  }
  if (gOriginalViewport != nullptr) {
    gOriginalViewport(x, y, width, height);
  }
}

void hookedRenderbufferStorage(GLenum target, GLenum internalFormat,
                               GLsizei width, GLsizei height) {
  if (gOriginalRenderbufferStorage != nullptr) {
    gOriginalRenderbufferStorage(target, internalFormat, width, height);
  }
  RenderPipeline::instance()
      .scaler()
      .noteRenderbufferStorage(internalFormat, width, height, 0);
}

void hookedRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                          GLenum internalFormat, GLsizei width,
                                          GLsizei height) {
  if (gOriginalRenderbufferStorageMultisample != nullptr) {
    gOriginalRenderbufferStorageMultisample(target, samples, internalFormat,
                                            width, height);
  }
  RenderPipeline::instance()
      .scaler()
      .noteRenderbufferStorage(internalFormat, width, height,
                               static_cast<int>(samples));
}

void hookedBindRenderbuffer(GLenum target, GLuint renderbuffer) {
  if (gOriginalBindRenderbuffer != nullptr) {
    gOriginalBindRenderbuffer(target, renderbuffer);
  }
  RenderPipeline::instance().scaler().noteBindRenderbuffer(renderbuffer);
}

void hookedBindTexture(GLenum target, GLuint texture) {
  if (gOriginalBindTexture != nullptr) {
    gOriginalBindTexture(target, texture);
  }
  RenderPipeline::instance().scaler().noteBindTexture(target, texture);
}

void hookedTexImage2D(GLenum target, GLint level, GLint internalFormat,
                      GLsizei width, GLsizei height, GLint border,
                      GLenum format, GLenum type, const void *pixels) {
  auto &scaler = RenderPipeline::instance().scaler();
  if (gOriginalTexImage2D != nullptr) {
    gOriginalTexImage2D(target, level, internalFormat, width, height, border,
                        format, type, pixels);
  }
  scaler.noteTexImage(static_cast<uint32_t>(internalFormat), level, width,
                      height);
}

void hookedTexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat,
                        GLsizei width, GLsizei height) {
  if (gOriginalTexStorage2D != nullptr) {
    gOriginalTexStorage2D(target, levels, internalFormat, width, height);
  }
  RenderPipeline::instance().scaler().noteTexStorage(
      static_cast<uint32_t>(internalFormat), levels, width, height);
}

void hookedDeleteTextures(GLsizei count, const GLuint *textures) {
  RenderPipeline::instance().scaler().noteDeleteTextures(
      reinterpret_cast<const uint32_t *>(textures), count);
  if (gOriginalDeleteTextures != nullptr) {
    gOriginalDeleteTextures(count, textures);
  }
}

void hookedFramebufferTexture2D(GLenum target, GLenum attachment,
                                GLenum textarget, GLuint texture, GLint level) {
  if (gOriginalFramebufferTexture2D != nullptr) {
    gOriginalFramebufferTexture2D(target, attachment, textarget, texture, level);
  }
  RenderPipeline::instance()
      .scaler()
      .noteFramebufferTexture(target, attachment, texture);
}

void hookedFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                   GLenum renderbufferTarget,
                                   GLuint renderbuffer) {
  if (gOriginalFramebufferRenderbuffer != nullptr) {
    gOriginalFramebufferRenderbuffer(target, attachment, renderbufferTarget,
                                     renderbuffer);
  }
  RenderPipeline::instance()
      .scaler()
      .noteFramebufferRenderbuffer(target, attachment, renderbuffer);
}

uint64_t monotonicNanoseconds() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// Maps an internal format to the format/type pair needed to re-specify the
// storage of a mutable texture.
bool storageFormatFor(GLenum internalFormat, GLenum *format, GLenum *type) {
  switch (internalFormat) {
  case GL_RGBA8:
  case GL_SRGB8_ALPHA8:
    *format = GL_RGBA;
    *type = GL_UNSIGNED_BYTE;
    return true;
  case GL_RGB8:
  case GL_SRGB8:
    *format = GL_RGB;
    *type = GL_UNSIGNED_BYTE;
    return true;
  case GL_RGBA16F:
    *format = GL_RGBA;
    *type = GL_HALF_FLOAT;
    return true;
  case GL_RGBA32F:
    *format = GL_RGBA;
    *type = GL_FLOAT;
    return true;
  case GL_RGB565:
    *format = GL_RGB;
    *type = GL_UNSIGNED_SHORT_5_6_5;
    return true;
  case GL_RGBA4:
    *format = GL_RGBA;
    *type = GL_UNSIGNED_SHORT_4_4_4_4;
    return true;
  default:
    break;
  }
  return false;
}

} // namespace

// ---------------------------------------------------------------------------
// SceneTargetScaler
// ---------------------------------------------------------------------------

void SceneTargetScaler::configure(const gfx::Api *api, bool enabled,
                                  int targetScalePercent) {
  mApi = api;
  mEnabled = enabled;
  mTargetScale = std::clamp(targetScalePercent, limits::kMinRenderScale,
                            limits::kMaxRenderScale);

  if (api == nullptr) {
    mUsable = false;
    mState.reason = "graphics API unavailable";
    return;
  }
  if (!enabled) {
    mUsable = false;
    mState.reason = "disabled by the user";
    return;
  }
  if (api->glBindFramebuffer == nullptr || api->glViewport == nullptr ||
      api->glTexImage2D == nullptr) {
    mUsable = false;
    mState.reason = "required GL entry points are missing";
    return;
  }
  mUsable = true;

  // A new target scale has to be applied to the already reduced target.
  if (mEnabled && mState.applied && mTargetScale != targetScalePercent) {
    mTargetScale = targetScalePercent;
    applyScale(mTargetScale);
  }
}

SceneTargetScaler::State SceneTargetScaler::state() const {
  std::lock_guard<std::mutex> lock(mStateMutex);
  return mState;
}

void SceneTargetScaler::setDisplaySize(int width, int height) {
  if (width == mDisplayWidth && height == mDisplayHeight) {
    return;
  }
  mDisplayWidth = width;
  mDisplayHeight = height;
  // The surface changed: the game recreates its targets, the previous
  // detection is not valid anymore.
  restoreOriginalSize();
  mState = State();
  mCandidateFramebuffer = 0;
  mTextureAttachments.clear();
  mRenderbufferAttachments.clear();
}

void SceneTargetScaler::reset() {
  restoreOriginalSize();
  mState = State();
  mCandidateFramebuffer = 0;
  releaseTracking();
}

void SceneTargetScaler::releaseTracking() {
  mRenderbuffers.clear();
  mTextures.clear();
  mTextureAttachments.clear();
  mRenderbufferAttachments.clear();
}

const SceneTargetScaler::RenderbufferInfo *
SceneTargetScaler::findRenderbuffer(uint32_t id) const {
  for (const auto &info : mRenderbuffers) {
    if (info.id == id) {
      return &info;
    }
  }
  return nullptr;
}

const SceneTargetScaler::TextureInfo *
SceneTargetScaler::findTexture(uint32_t id) const {
  if (id == 0) {
    return nullptr;
  }
  for (const auto &info : mTextures) {
    if (info.id == id) {
      return &info;
    }
  }
  return nullptr;
}

const SceneTargetScaler::TextureAttachment *
SceneTargetScaler::findTextureAttachment(uint32_t framebuffer,
                                         uint32_t attachment) const {
  for (const auto &entry : mTextureAttachments) {
    if (entry.framebuffer == framebuffer && entry.attachment == attachment) {
      return &entry;
    }
  }
  return nullptr;
}

const SceneTargetScaler::RenderbufferAttachment *
SceneTargetScaler::findRenderbufferAttachment(uint32_t framebuffer,
                                               uint32_t attachment) const {
  for (const auto &entry : mRenderbufferAttachments) {
    if (entry.framebuffer == framebuffer && entry.attachment == attachment) {
      return &entry;
    }
  }
  return nullptr;
}

void SceneTargetScaler::noteFramebufferBind(uint32_t framebuffer) {
  mBoundFramebuffer = framebuffer;

  // The game may set the viewport before it binds the target; in that case the
  // viewport that belongs to the newly bound framebuffer is the last one that
  // was set.
  if (mDisplayWidth > 0 && mDisplayHeight > 0 && mViewportWidth > 0 &&
      mViewportHeight > 0 && framebuffer != 0) {
    const double widthRatio =
        static_cast<double>(mViewportWidth) / static_cast<double>(mDisplayWidth);
    const double heightRatio =
        static_cast<double>(mViewportHeight) / static_cast<double>(mDisplayHeight);
    if (widthRatio > 0.98 && widthRatio < 1.02 && heightRatio > 0.98 &&
        heightRatio < 1.02) {
      mCandidateFramebuffer = framebuffer;
      if (mEnabled && mTargetScale < 100 && !mState.applied) {
        applyScale(mTargetScale);
      }
    }
  }
}

void SceneTargetScaler::noteViewport(int x, int y, int width, int height) {
  (void)x;
  (void)y;
  mViewportWidth = width;
  mViewportHeight = height;

  if (!mUsable || !mEnabled || mDisplayWidth <= 0 || mDisplayHeight <= 0) {
    return;
  }
  if (mBoundFramebuffer == 0 || width <= 0 || height <= 0) {
    return;
  }

  const double widthRatio =
      static_cast<double>(width) / static_cast<double>(mDisplayWidth);
  const double heightRatio =
      static_cast<double>(height) / static_cast<double>(mDisplayHeight);
  if (widthRatio <= 0.98 || widthRatio >= 1.02 || heightRatio <= 0.98 ||
      heightRatio >= 1.02) {
    return;
  }

  mCandidateFramebuffer = mBoundFramebuffer;

  if (mTargetScale >= 100) {
    return;
  }
  if (mState.applied && mState.framebuffer == mCandidateFramebuffer) {
    return;
  }
  applyScale(mTargetScale);
}

void SceneTargetScaler::clampViewport(int &x, int &y, int &width,
                                      int &height) {
  if (!mUsable || !mEnabled || !mState.applied) {
    return;
  }
  if (mBoundFramebuffer != mState.framebuffer) {
    return;
  }
  if (width > mState.scaledWidth) {
    width = mState.scaledWidth;
  }
  if (height > mState.scaledHeight) {
    height = mState.scaledHeight;
  }
  (void)x;
  (void)y;
}

void SceneTargetScaler::noteBindRenderbuffer(uint32_t renderbuffer) {
  mBoundRenderbuffer = renderbuffer;
}

void SceneTargetScaler::noteRenderbufferStorage(uint32_t internalFormat,
                                                int width, int height,
                                                int samples) {
  if (mBoundRenderbuffer == 0) {
    return;
  }
  for (auto &info : mRenderbuffers) {
    if (info.id == mBoundRenderbuffer) {
      info.internalFormat = internalFormat;
      info.width = width;
      info.height = height;
      info.samples = samples;
      return;
    }
  }
  RenderbufferInfo info;
  info.id = mBoundRenderbuffer;
  info.internalFormat = internalFormat;
  info.width = width;
  info.height = height;
  info.samples = samples;
  mRenderbuffers.push_back(info);
}

void SceneTargetScaler::noteBindTexture(uint32_t target, uint32_t texture) {
  if (target == GL_TEXTURE_2D) {
    mBoundTexture2D = texture;
  }
}

void SceneTargetScaler::noteTexImage(uint32_t internalFormat, int level,
                                     int width, int height) {
  if (mBoundTexture2D == 0 || level != 0) {
    return;
  }
  for (auto &info : mTextures) {
    if (info.id == mBoundTexture2D) {
      info.internalFormat = internalFormat;
      info.width = width;
      info.height = height;
      info.levels = 1;
      info.immutable = false;
      return;
    }
  }
  TextureInfo info;
  info.id = mBoundTexture2D;
  info.internalFormat = internalFormat;
  info.width = width;
  info.height = height;
  info.levels = 1;
  info.immutable = false;
  mTextures.push_back(info);
}

void SceneTargetScaler::noteTexStorage(uint32_t internalFormat, int levels,
                                       int width, int height) {
  if (mBoundTexture2D == 0) {
    return;
  }
  for (auto &info : mTextures) {
    if (info.id == mBoundTexture2D) {
      info.internalFormat = internalFormat;
      info.width = width;
      info.height = height;
      info.levels = levels;
      info.immutable = true;
      return;
    }
  }
  TextureInfo info;
  info.id = mBoundTexture2D;
  info.internalFormat = internalFormat;
  info.width = width;
  info.height = height;
  info.levels = levels;
  info.immutable = true;
  mTextures.push_back(info);
}

void SceneTargetScaler::noteDeleteTextures(const uint32_t *textures,
                                           int count) {
  if (textures == nullptr) {
    return;
  }
  // A deleted texture id may be reused by the game, the record has to go.
  for (int i = 0; i < count; ++i) {
    const uint32_t id = textures[i];
    mTextures.erase(std::remove_if(mTextures.begin(), mTextures.end(),
                                   [id](const TextureInfo &info) {
                                     return info.id == id;
                                   }),
                    mTextures.end());
    mTextureAttachments.erase(
        std::remove_if(mTextureAttachments.begin(), mTextureAttachments.end(),
                       [id](const TextureAttachment &entry) {
                         return entry.texture == id;
                       }),
        mTextureAttachments.end());
    if (mState.colorTexture == id) {
      // The tracked target is gone; stop scaling until it is detected again.
      mState = State();
    }
  }
}

void SceneTargetScaler::noteFramebufferTexture(uint32_t framebuffer,
                                               uint32_t attachment,
                                               uint32_t texture) {
  for (auto &entry : mTextureAttachments) {
    if (entry.framebuffer == framebuffer && entry.attachment == attachment) {
      entry.texture = texture;
      return;
    }
  }
  mTextureAttachments.push_back({framebuffer, attachment, texture});
}

void SceneTargetScaler::noteFramebufferRenderbuffer(uint32_t framebuffer,
                                                     uint32_t attachment,
                                                     uint32_t renderbuffer) {
  for (auto &entry : mRenderbufferAttachments) {
    if (entry.framebuffer == framebuffer && entry.attachment == attachment) {
      entry.renderbuffer = renderbuffer;
      return;
    }
  }
  mRenderbufferAttachments.push_back({framebuffer, attachment, renderbuffer});
}

bool SceneTargetScaler::resizeTexture(uint32_t texture, uint32_t internalFormat,
                                      int width, int height) {
  if (mApi == nullptr || mApi->glTexImage2D == nullptr) {
    return false;
  }
  GLenum format = GL_RGBA;
  GLenum type = GL_UNSIGNED_BYTE;
  if (!storageFormatFor(static_cast<GLenum>(internalFormat), &format, &type)) {
    return false;
  }

  // Clear pending errors so the result of the call can be evaluated.
  while (mApi->glGetError != nullptr && mApi->glGetError() != GL_NO_ERROR) {
  }

  mApi->glBindTexture(GL_TEXTURE_2D, texture);
  mApi->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat),
                      width, height, 0, format, type, nullptr);

  if (mApi->glGetError != nullptr) {
    const GLenum error = mApi->glGetError();
    if (error != GL_NO_ERROR) {
      // The texture is immutable (allocated with glTexStorage2D) or uses a
      // format that cannot be reproduced. The texture is left untouched, the
      // module simply does not scale this target.
      return false;
    }
  }
  return true;
}

bool SceneTargetScaler::applyScale(int percent) {
  if (mApi == nullptr || mCandidateFramebuffer == 0) {
    return false;
  }

  const TextureAttachment *color = findTextureAttachment(mCandidateFramebuffer,
                                                          GL_COLOR_ATTACHMENT0);
  if (color == nullptr || color->texture == 0) {
    mState.detected = true;
    mState.reason = "the scene target has no texture colour attachment";
    return false;
  }

  // OpenGL ES 3 has no API to query the size or the internal format of a
  // texture, so both come from the intercepted allocation calls.
  const TextureInfo *info = findTexture(color->texture);
  if (info == nullptr || info->width <= 0 || info->height <= 0 ||
      info->internalFormat == 0) {
    mState.detected = true;
    mState.reason = "the scene target parameters are not known";
    return false;
  }

  // An immutable (glTexStorage2D) or mipmapped target is left alone: the
  // storage cannot be re-specified, and resizing would leave the game
  // sampling mip levels that no longer exist.
  if (info->immutable) {
    mState.detected = true;
    mState.reason = "immutable scene target is not supported";
    return false;
  }
  if (info->levels > 1) {
    mState.detected = true;
    mState.reason = "mipmapped scene target is not supported";
    return false;
  }

  const int width = info->width;
  const int height = info->height;
  const uint32_t internalFormat = info->internalFormat;

  // The base size is the size the game allocated; a later change of the
  // target scale is always calculated from that base size.
  int baseWidth = width;
  int baseHeight = height;
  if (mState.applied && mState.framebuffer == mCandidateFramebuffer) {
    baseWidth = mState.originalWidth;
    baseHeight = mState.originalHeight;
  }

  const int newWidth =
      std::max(1, (static_cast<int>(baseWidth) * percent + 50) / 100);
  const int newHeight =
      std::max(1, (static_cast<int>(baseHeight) * percent + 50) / 100);
  if (newWidth == width && newHeight == height) {
    // The target already has the requested size.
    mState.detected = true;
    mState.applied = true;
    mState.verified = true;
    mState.framebuffer = mCandidateFramebuffer;
    mState.colorTexture = color->texture;
    mState.originalWidth = baseWidth;
    mState.originalHeight = baseHeight;
    mState.scaledWidth = width;
    mState.scaledHeight = height;
    return true;
  }

  // The depth attachment has to use the same size, otherwise the framebuffer
  // becomes incomplete.
  const RenderbufferAttachment *depthAttachment =
      findRenderbufferAttachment(mCandidateFramebuffer, GL_DEPTH_ATTACHMENT);
  const RenderbufferInfo *depthInfo = nullptr;
  if (depthAttachment != nullptr) {
    depthInfo = findRenderbuffer(depthAttachment->renderbuffer);
    if (depthInfo == nullptr || depthInfo->samples > 0) {
      mState.detected = true;
      mState.reason = "multisampled scene target is not supported";
      return false;
    }
    if (depthInfo->width != width || depthInfo->height != height) {
      mState.detected = true;
      mState.reason = "depth attachment does not match the scene target";
      return false;
    }
  }

  // Resize the depth renderbuffer first, then the colour texture.
  if (depthInfo != nullptr && mApi->glRenderbufferStorage != nullptr) {
    mApi->glBindRenderbuffer(GL_RENDERBUFFER, depthAttachment->renderbuffer);
    mApi->glRenderbufferStorage(GL_RENDERBUFFER,
                                 static_cast<GLenum>(depthInfo->internalFormat),
                                 newWidth, newHeight);
    if (mApi->glGetError != nullptr && mApi->glGetError() != GL_NO_ERROR) {
      mState.detected = true;
      mState.reason = "the depth attachment could not be resized";
      return false;
    }
    for (auto &info : mRenderbuffers) {
      if (info.id == depthAttachment->renderbuffer) {
        info.width = newWidth;
        info.height = newHeight;
      }
    }
  }

  if (!resizeTexture(color->texture, internalFormat, newWidth, newHeight)) {
    mState.detected = true;
    mState.reason = "the scene target could not be resized";
    return false;
  }

  // Keep the tracked record in sync with the new allocation.
  for (auto &tracked : mTextures) {
    if (tracked.id == color->texture) {
      tracked.width = newWidth;
      tracked.height = newHeight;
    }
  }

  // The framebuffer has to stay complete.
  if (mApi->glCheckFramebufferStatus != nullptr) {
    const GLenum status = mApi->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      restoreOriginalSize();
      mState.detected = true;
      mState.reason = "the reduced target is incomplete, reverted";
      return false;
    }
  }

  mState.detected = true;
  mState.applied = true;
  mState.verified = false;
  mState.framebuffer = mCandidateFramebuffer;
  mState.colorTexture = color->texture;
  mState.depthRenderbuffer =
      depthAttachment != nullptr ? depthAttachment->renderbuffer : 0;
  mState.originalWidth = baseWidth;
  mState.originalHeight = baseHeight;
  mState.scaledWidth = newWidth;
  mState.scaledHeight = newHeight;
  mState.reason.clear();
  return true;
}

void SceneTargetScaler::restoreOriginalSize() {
  if (mApi == nullptr || !mState.applied) {
    return;
  }
  if (mState.colorTexture != 0) {
    const TextureInfo *info = findTexture(mState.colorTexture);
    if (info != nullptr && info->internalFormat != 0) {
      if (resizeTexture(mState.colorTexture, info->internalFormat,
                        mState.originalWidth, mState.originalHeight)) {
        for (auto &tracked : mTextures) {
          if (tracked.id == mState.colorTexture) {
            tracked.width = mState.originalWidth;
            tracked.height = mState.originalHeight;
          }
        }
      }
    }
  }
  if (mState.depthRenderbuffer != 0) {
    const RenderbufferInfo *info = findRenderbuffer(mState.depthRenderbuffer);
    if (info != nullptr && mApi->glRenderbufferStorage != nullptr) {
      mApi->glBindRenderbuffer(GL_RENDERBUFFER, mState.depthRenderbuffer);
      mApi->glRenderbufferStorage(GL_RENDERBUFFER,
                                   static_cast<GLenum>(info->internalFormat),
                                   mState.originalWidth, mState.originalHeight);
    }
  }
  mState.applied = false;
  mState.scaledWidth = 0;
  mState.scaledHeight = 0;
}

void SceneTargetScaler::onFrameEnd() {
  if (!mUsable || !mEnabled) {
    return;
  }
  if (mState.applied && !mState.verified) {
    // The frame was rendered with the reduced target, verify that it is still
    // complete before the next frame uses it.
    if (mApi != nullptr && mApi->glCheckFramebufferStatus != nullptr &&
        mState.framebuffer != 0) {
      const GLenum status = mApi->glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE) {
        mState.reason = "the reduced target is incomplete, reverted";
        restoreOriginalSize();
        mState.detected = false;
        return;
      }
    }
    mState.verified = true;
  }
}

// ---------------------------------------------------------------------------
// RenderPipeline
// ---------------------------------------------------------------------------

RenderPipeline &RenderPipeline::instance() {
  static RenderPipeline pipeline;
  return pipeline;
}

namespace {

// Resolves an EGL entry point.
//
// dlopen("libEGL.so") is tried first because dlsym(RTLD_DEFAULT) only sees
// the global scope, and libEGL is not guaranteed to be in it. eglGetProcAddress
// is the last resort: for core EGL 1.x functions it usually returns the same
// address, but it may return a dispatch wrapper instead of the symbol the
// game calls through.
void *resolveEglSymbol(const char *name, void *eglHandle) {
  if (eglHandle != nullptr) {
    if (void *symbol = dlsym(eglHandle, name)) {
      return symbol;
    }
  }
  if (void *symbol = dlsym(RTLD_DEFAULT, name)) {
    return symbol;
  }
  const auto eglGetProcAddress =
      reinterpret_cast<gfx::EglGetProcAddressFn>(
          dlsym(RTLD_DEFAULT, "eglGetProcAddress"));
  if (eglGetProcAddress != nullptr) {
    return eglGetProcAddress(name);
  }
  return nullptr;
}

} // namespace

bool RenderPipeline::install() {
  if (mHooked) {
    return true;
  }

  // Allow a re-enable after a disable.
  mShutdownRequested.store(false, std::memory_order_relaxed);

  void *eglHandle = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
  if (eglHandle != nullptr) {
    mEglHandle = eglHandle;
  }

  void *swapAddress = resolveEglSymbol("eglSwapBuffers", eglHandle);
  if (swapAddress == nullptr) {
    mFallbackReason = "eglSwapBuffers is not available in this process";
    return false;
  }

  mOriginalSwapBuffers = nullptr;
  const int result = pl::memory::hook(
      swapAddress, reinterpret_cast<void *>(&RenderPipeline::swapTrampoline),
      reinterpret_cast<void **>(&mOriginalSwapBuffers),
      pl::memory::HookPriority::High);
  if (result != 0 || mOriginalSwapBuffers == nullptr) {
    mOriginalSwapBuffers = nullptr;
    mFallbackReason = "the presentation hook could not be installed";
    return false;
  }
  mSwapAddress = swapAddress;

  mHooked = true;

  // The GL entry points are resolved lazily: the context does not exist yet
  // while the module is being loaded.
  mApi.eglGetProcAddress = reinterpret_cast<gfx::EglGetProcAddressFn>(
      resolveEglSymbol("eglGetProcAddress", eglHandle));
  return true;
}

void RenderPipeline::uninstall() {
  mShutdownRequested.store(true);
  releaseContextResources();

  gBindFramebufferHook.reset();
  gViewportHook.reset();
  gRenderbufferStorageHook.reset();
  gBindRenderbufferHook.reset();
  gTexImage2DHook.reset();
  gTexStorage2DHook.reset();
  gDeleteTexturesHook.reset();
  gBindTextureHook.reset();
  gFramebufferTexture2DHook.reset();
  gFramebufferRenderbufferHook.reset();
  gOriginalBindFramebuffer = nullptr;
  gOriginalViewport = nullptr;
  gOriginalRenderbufferStorage = nullptr;
  gOriginalBindRenderbuffer = nullptr;
  gOriginalTexImage2D = nullptr;
  gOriginalTexStorage2D = nullptr;
  gOriginalDeleteTextures = nullptr;
  gOriginalBindTexture = nullptr;
  gOriginalFramebufferTexture2D = nullptr;
  gOriginalFramebufferRenderbuffer = nullptr;

  if (mHooked && mSwapAddress != nullptr) {
    pl::memory::unhook(mSwapAddress,
                       reinterpret_cast<void *>(&RenderPipeline::swapTrampoline));
  }
  mHooked = false;
  mSwapAddress = nullptr;
  mOriginalSwapBuffers = nullptr;

  if (mEglHandle != nullptr) {
    dlclose(mEglHandle);
    mEglHandle = nullptr;
  }
}

EGLBoolean RenderPipeline::swapTrampoline(EGLDisplay display,
                                           EGLSurface surface) {
  RenderPipeline &self = RenderPipeline::instance();
  self.mSwapCounter.fetch_add(1, std::memory_order_relaxed);
  self.onPresent(display, surface);
  if (self.mOriginalSwapBuffers != nullptr) {
    return self.mOriginalSwapBuffers(display, surface);
  }
  return EGL_FALSE;
}

void RenderPipeline::onPresent(EGLDisplay display, EGLSurface surface) {
  const uint64_t now = monotonicNanoseconds();
  updateTimings(now);

  if (mShutdownRequested.load(std::memory_order_relaxed)) {
    return;
  }

  if (mSettingsDirty.exchange(false, std::memory_order_acq_rel)) {
    std::lock_guard<std::mutex> lock(mSettingsMutex);
    const bool timingsWereEnabled = mSettings.gpuTimings;
    mSettings = mPendingSettings;
    if (timingsWereEnabled && !mSettings.gpuTimings) {
      mGpuTimingValid.store(false, std::memory_order_relaxed);
      mGpuTimeMs.store(0.0, std::memory_order_relaxed);
    }
    mDynamicResolution.configure(mSettings.dynamicResolution,
                                 mSettings.targetFps, mSettings.minRenderScale,
                                 mSettings.maxRenderScale, mSettings.renderScale);
  }

  if (!mSettings.masterEnabled) {
    std::lock_guard<std::mutex> lock(mStatusMutex);
    mPath = ActivePath::Disabled;
    return;
  }

  if (!ensureContextResources()) {
    return;
  }

  updateSurfaceSize(display, surface);
  mScaler.setDisplaySize(mSurfaceWidth.load(std::memory_order_relaxed),
                          mSurfaceHeight.load(std::memory_order_relaxed));
  mScaler.onFrameEnd();

  // The dynamic resolution controller works on the measured frame time and
  // drives the scale of the scene target.
  if (mSettings.dynamicResolution && mScaler.state().applied) {
    mDynamicResolution.submitFrame(mFrameTimeMs);
  }
  const int effectiveScale = mDynamicResolution.enabled()
                                 ? mDynamicResolution.currentScale()
                                 : mSettings.renderScale;
  mScaler.configure(&mApi, mSettings.internalResolutionControl, effectiveScale);

  const bool wantReconstruction =
      mSettings.upscalingEnabled && mSettings.mode != UpscaleMode::Native;
  if (wantReconstruction) {
    runReconstructionPass();
  }

  {
    std::lock_guard<std::mutex> lock(mStatusMutex);
    if (mScaler.state().applied) {
      mPath = ActivePath::InternalResolution;
    } else if (wantReconstruction) {
      mPath = ActivePath::Reconstruction;
    } else {
      mPath = ActivePath::Native;
    }
  }

  std::function<void()> callback;
  {
    std::lock_guard<std::mutex> lock(mFrameCallbackMutex);
    callback = mFrameCallback;
  }
  if (callback) {
    callback();
  }
}

void RenderPipeline::updateTimings(uint64_t nowNs) {
  mFrameCounter.fetch_add(1, std::memory_order_relaxed);
  if (mLastFrameNs != 0 && nowNs > mLastFrameNs) {
    const double deltaMs =
        static_cast<double>(nowNs - mLastFrameNs) / 1000000.0;
    // Ignore absurd values caused by a suspended process or a debugger.
    if (deltaMs > 0.05 && deltaMs < 5000.0) {
      mFrameTimeMs.store(deltaMs, std::memory_order_relaxed);
      mFps.store(deltaMs > 0.0 ? 1000.0 / deltaMs : 0.0,
                 std::memory_order_relaxed);
    }
  }
  mLastFrameNs = nowNs;
}

bool RenderPipeline::ensureContextResources() {
  EGLContext context = EGL_NO_CONTEXT;
  if (mApi.eglGetCurrentContext != nullptr) {
    context = mApi.eglGetCurrentContext();
  }
  if (context == EGL_NO_CONTEXT) {
    // The game is not presenting through EGL (Vulkan or a headless context).
    {
      std::lock_guard<std::mutex> lock(mStatusMutex);
      mPath = ActivePath::Native;
      mFallbackReason = "the game does not use an EGL context";
    }
    return false;
  }

  if (context != mContext) {
    // New context (first frame or after a context loss): drop every GPU
    // resource, they belong to the old context.
    releaseContextResources();
    mContext = context;

    // The entry points belong to this context, so they are resolved again
    // whenever the context changes.
    mApi.eglGetProcAddress = reinterpret_cast<gfx::EglGetProcAddressFn>(
        resolveEglSymbol("eglGetProcAddress", mEglHandle));
    gfx::loadApi(mApi, mApi.eglGetProcAddress);
    if (!mApi.complete) {
      {
        std::lock_guard<std::mutex> lock(mStatusMutex);
        mPath = ActivePath::Native;
        mFallbackReason = "the OpenGL ES entry points are not available";
      }
      return false;
    }

    mGpuInfo = gfx::detectCapabilities(mApi);
    mGpuInfo.backend = GraphicsBackend::OpenGLES;
    mTimerQuerySupported =
        mGpuInfo.hasTimerQuery && mApi.glBeginQuery != nullptr &&
        mApi.glGetQueryObjectuiv != nullptr && mApi.glGenQueries != nullptr;
    if (mTimerQuerySupported) {
      mApi.glGenQueries(1, &mTimerQuery);
    }
  }

  if (!mContextReady) {
    if (!ensureProgram()) {
      {
        std::lock_guard<std::mutex> lock(mStatusMutex);
        mPath = ActivePath::Native;
        mFallbackReason = "the reconstruction shader could not be compiled";
      }
      return false;
    }
    mContextReady = true;
  }

  // Install the GL interception used by the internal resolution controller.
  if (mSettings.internalResolutionControl && !gBindFramebufferHook.installed()) {
    gOriginalBindFramebuffer = mApi.glBindFramebuffer;
    gOriginalViewport = mApi.glViewport;
    gOriginalRenderbufferStorage = mApi.glRenderbufferStorage;
    gOriginalRenderbufferStorageMultisample = mApi.glRenderbufferStorageMultisample;
    gOriginalBindRenderbuffer = mApi.glBindRenderbuffer;
    gOriginalTexImage2D = mApi.glTexImage2D;
    gOriginalTexStorage2D = mApi.glTexStorage2D;
    gOriginalDeleteTextures = mApi.glDeleteTextures;
    gOriginalBindTexture = mApi.glBindTexture;
    gOriginalFramebufferTexture2D = mApi.glFramebufferTexture2D;
    gOriginalFramebufferRenderbuffer = mApi.glFramebufferRenderbuffer;

    if (gOriginalBindFramebuffer != nullptr) {
      gBindFramebufferHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalBindFramebuffer),
          reinterpret_cast<void *>(&hookedBindFramebuffer),
          reinterpret_cast<void **>(&gOriginalBindFramebuffer),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalViewport != nullptr) {
      gViewportHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalViewport),
          reinterpret_cast<void *>(&hookedViewport),
          reinterpret_cast<void **>(&gOriginalViewport),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalRenderbufferStorage != nullptr) {
      gRenderbufferStorageHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalRenderbufferStorage),
          reinterpret_cast<void *>(&hookedRenderbufferStorage),
          reinterpret_cast<void **>(&gOriginalRenderbufferStorage),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalBindRenderbuffer != nullptr) {
      gBindRenderbufferHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalBindRenderbuffer),
          reinterpret_cast<void *>(&hookedBindRenderbuffer),
          reinterpret_cast<void **>(&gOriginalBindRenderbuffer),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalTexImage2D != nullptr) {
      gTexImage2DHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalTexImage2D),
          reinterpret_cast<void *>(&hookedTexImage2D),
          reinterpret_cast<void **>(&gOriginalTexImage2D),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalTexStorage2D != nullptr) {
      gTexStorage2DHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalTexStorage2D),
          reinterpret_cast<void *>(&hookedTexStorage2D),
          reinterpret_cast<void **>(&gOriginalTexStorage2D),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalDeleteTextures != nullptr) {
      gDeleteTexturesHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalDeleteTextures),
          reinterpret_cast<void *>(&hookedDeleteTextures),
          reinterpret_cast<void **>(&gOriginalDeleteTextures),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalBindTexture != nullptr) {
      gBindTextureHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalBindTexture),
          reinterpret_cast<void *>(&hookedBindTexture),
          reinterpret_cast<void **>(&gOriginalBindTexture),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalFramebufferTexture2D != nullptr) {
      gFramebufferTexture2DHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalFramebufferTexture2D),
          reinterpret_cast<void *>(&hookedFramebufferTexture2D),
          reinterpret_cast<void **>(&gOriginalFramebufferTexture2D),
          pl::memory::HookPriority::Normal);
    }
    if (gOriginalFramebufferRenderbuffer != nullptr) {
      gFramebufferRenderbufferHook = pl::memory::HookHandle(
          reinterpret_cast<void *>(gOriginalFramebufferRenderbuffer),
          reinterpret_cast<void *>(&hookedFramebufferRenderbuffer),
          reinterpret_cast<void **>(&gOriginalFramebufferRenderbuffer),
          pl::memory::HookPriority::Normal);
    }
  }

  mScaler.configure(&mApi, mSettings.internalResolutionControl,
                    mSettings.renderScale);
  return true;
}

void RenderPipeline::releaseContextResources() {
  if (mApi.glDeleteProgram != nullptr && mProgram != 0) {
    mApi.glDeleteProgram(mProgram);
  }
  if (mApi.glDeleteFramebuffers != nullptr && mCaptureFramebuffer != 0) {
    mApi.glDeleteFramebuffers(1, &mCaptureFramebuffer);
  }
  if (mApi.glDeleteTextures != nullptr && mCaptureTexture != 0) {
    mApi.glDeleteTextures(1, &mCaptureTexture);
  }
  if (mApi.glDeleteVertexArrays != nullptr && mVertexArray != 0) {
    mApi.glDeleteVertexArrays(1, &mVertexArray);
  }
  if (mApi.glDeleteQueries != nullptr && mTimerQuery != 0) {
    mApi.glDeleteQueries(1, &mTimerQuery);
  }
  mProgram = 0;
  mCaptureFramebuffer = 0;
  mCaptureTexture = 0;
  mVertexArray = 0;
  mTimerQuery = 0;
  mContextReady = false;
  mContext = EGL_NO_CONTEXT;
  mSurfaceKnown = false;
}

bool RenderPipeline::ensureProgram() {
  if (mApi.glCreateShader == nullptr || mApi.glCreateProgram == nullptr) {
    return false;
  }

  const auto compile = [this](GLenum type, const char *source,
                              std::string &log) -> GLuint {
    const GLuint shader = mApi.glCreateShader(type);
    if (shader == 0) {
      return 0;
    }
    const GLchar *sources[] = {source};
    mApi.glShaderSource(shader, 1, sources, nullptr);
    mApi.glCompileShader(shader);
    GLint status = GL_FALSE;
    mApi.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
      GLint length = 0;
      mApi.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
      if (length > 1 && length < 4096 && mApi.glGetShaderInfoLog != nullptr) {
        std::string buffer(static_cast<std::size_t>(length), '\0');
        mApi.glGetShaderInfoLog(shader, length, nullptr, buffer.data());
        log = buffer;
      }
      mApi.glDeleteShader(shader);
      return 0;
    }
    return shader;
  };

  std::string vertexLog;
  std::string fragmentLog;
  const GLuint vertexShader =
      compile(GL_VERTEX_SHADER, shaders::kFullscreenVertexShader, vertexLog);
  if (vertexShader == 0) {
    mProgramLog = "vertex shader: " + vertexLog;
    return false;
  }
  const GLuint fragmentShader = compile(GL_FRAGMENT_SHADER,
                                        shaders::kReconstructionFragmentShader,
                                        fragmentLog);
  if (fragmentShader == 0) {
    mApi.glDeleteShader(vertexShader);
    mProgramLog = "fragment shader: " + fragmentLog;
    return false;
  }

  const GLuint program = mApi.glCreateProgram();
  mApi.glAttachShader(program, vertexShader);
  mApi.glAttachShader(program, fragmentShader);
  mApi.glLinkProgram(program);
  mApi.glDeleteShader(vertexShader);
  mApi.glDeleteShader(fragmentShader);

  GLint linked = GL_FALSE;
  mApi.glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint length = 0;
    mApi.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length > 1 && length < 4096 && mApi.glGetProgramInfoLog != nullptr) {
      std::string buffer(static_cast<std::size_t>(length), '\0');
      mApi.glGetProgramInfoLog(program, length, nullptr, buffer.data());
      mProgramLog = "link: " + buffer;
    } else {
      mProgramLog = "link failed";
    }
    mApi.glDeleteProgram(program);
    return false;
  }

  mProgram = program;
  mUniformSource = mApi.glGetUniformLocation(program, "uSource");
  mUniformDetail = mApi.glGetUniformLocation(program, "uDetail");
  mUniformOutputSize = mApi.glGetUniformLocation(program, "uOutputSize");
  mUniformSourceSize = mApi.glGetUniformLocation(program, "uSourceSize");
  mUniformDetailSize = mApi.glGetUniformLocation(program, "uDetailSize");
  mUniformSharpness = mApi.glGetUniformLocation(program, "uSharpness");
  mUniformDetailStrength =
      mApi.glGetUniformLocation(program, "uDetailStrength");
  mUniformUseDetail = mApi.glGetUniformLocation(program, "uUseDetail");

  if (mApi.glGenVertexArrays != nullptr) {
    mApi.glGenVertexArrays(1, &mVertexArray);
  }
  return true;
}

void RenderPipeline::updateSurfaceSize(EGLDisplay display, EGLSurface surface) {
  if (mApi.eglQuerySurface == nullptr) {
    return;
  }
  EGLint width = 0;
  EGLint height = 0;
  if (mApi.eglQuerySurface(display, surface, EGL_WIDTH, &width) != EGL_TRUE ||
      mApi.eglQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE) {
    return;
  }
  if (width <= 0 || height <= 0) {
    return;
  }
  if (mSurfaceKnown.load(std::memory_order_relaxed) &&
      width == mSurfaceWidth.load(std::memory_order_relaxed) &&
      height == mSurfaceHeight.load(std::memory_order_relaxed)) {
    return;
  }
  mSurfaceWidth.store(width, std::memory_order_relaxed);
  mSurfaceHeight.store(height, std::memory_order_relaxed);
  mSurfaceKnown.store(true, std::memory_order_relaxed);
  mScaler.setDisplaySize(width, height);
  notifySurfaceResized();
}

void RenderPipeline::notifySurfaceResized() {
  // The capture target has to match the new surface size.
  if (mApi.glDeleteTextures != nullptr && mCaptureTexture != 0) {
    mApi.glDeleteTextures(1, &mCaptureTexture);
    mCaptureTexture = 0;
  }
  if (mApi.glDeleteFramebuffers != nullptr && mCaptureFramebuffer != 0) {
    mApi.glDeleteFramebuffers(1, &mCaptureFramebuffer);
    mCaptureFramebuffer = 0;
  }
}

void RenderPipeline::runReconstructionPass() {
  const int surfaceWidth = mSurfaceWidth.load(std::memory_order_relaxed);
  const int surfaceHeight = mSurfaceHeight.load(std::memory_order_relaxed);
  if (surfaceWidth <= 0 || surfaceHeight <= 0 || mProgram == 0) {
    return;
  }

  if (mCaptureTexture == 0) {
    mApi.glGenTextures(1, &mCaptureTexture);
    mApi.glBindTexture(GL_TEXTURE_2D, mCaptureTexture);
    mApi.glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, surfaceWidth,
                        surfaceHeight);
    mApi.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    mApi.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    mApi.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    mApi.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mApi.glGenFramebuffers(1, &mCaptureFramebuffer);
    mApi.glBindFramebuffer(GL_FRAMEBUFFER, mCaptureFramebuffer);
    mApi.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, mCaptureTexture, 0);
  }

  // Save the state the game left behind, the module restores it afterwards.
  GLint viewport[4] = {0, 0, 0, 0};
  GLint previousFramebuffer = 0;
  GLint previousProgram = 0;
  GLint previousArrayBuffer = 0;
  GLint previousVertexArray = 0;
  GLint previousActiveTexture = 0;
  GLint previousTexture0 = 0;
  GLboolean blend = GL_FALSE;
  GLboolean depthTest = GL_FALSE;
  GLboolean cullFace = GL_FALSE;
  GLboolean scissorTest = GL_FALSE;
  mApi.glGetIntegerv(GL_VIEWPORT, viewport);
  mApi.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  mApi.glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  mApi.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
  mApi.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
  mApi.glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  mApi.glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
  blend = mApi.glIsEnabled(GL_BLEND);
  depthTest = mApi.glIsEnabled(GL_DEPTH_TEST);
  cullFace = mApi.glIsEnabled(GL_CULL_FACE);
  scissorTest = mApi.glIsEnabled(GL_SCISSOR_TEST);

  const uint64_t start = mSettings.gpuTimings ? monotonicNanoseconds() : 0;

  if (mSettings.gpuTimings && mTimerQuerySupported && mTimerQuery != 0 &&
      !mTimerQueryActive) {
    collectGpuTiming();
    mApi.glBeginQuery(GL_TIME_ELAPSED, mTimerQuery);
    mTimerQueryActive = true;
  }

  // 1. Copy the presented frame into the module owned texture. This stays on
  //    the GPU, there is no read back to the CPU.
  mApi.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  mApi.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mCaptureFramebuffer);
  mApi.glBlitFramebuffer(0, 0, surfaceWidth, surfaceHeight, 0, 0, surfaceWidth,
                         surfaceHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);

  // 2. Reconstruct and sharpen into the swap chain.
  mApi.glBindFramebuffer(GL_FRAMEBUFFER, 0);
  mApi.glViewport(0, 0, surfaceWidth, surfaceHeight);
  mApi.glDisable(GL_BLEND);
  mApi.glDisable(GL_DEPTH_TEST);
  mApi.glDisable(GL_CULL_FACE);
  mApi.glDisable(GL_SCISSOR_TEST);
  mApi.glUseProgram(mProgram);
  if (mVertexArray != 0) {
    mApi.glBindVertexArray(mVertexArray);
  }

  mApi.glActiveTexture(GL_TEXTURE0);
  mApi.glBindTexture(GL_TEXTURE_2D, mCaptureTexture);
  if (mUniformSource >= 0) {
    mApi.glUniform1i(mUniformSource, 0);
  }

  bool useDetail = false;
  float detailStrength = 0.0f;
  int detailWidth = 0;
  int detailHeight = 0;
  const auto scalerState = mScaler.state();
  if (mSettings.detailRecovery && scalerState.applied &&
      scalerState.colorTexture != 0 && scalerState.scaledWidth > 0 &&
      scalerState.scaledHeight > 0) {
    mApi.glActiveTexture(GL_TEXTURE1);
    mApi.glBindTexture(GL_TEXTURE_2D, scalerState.colorTexture);
    if (mUniformDetail >= 0) {
      mApi.glUniform1i(mUniformDetail, 1);
    }
    useDetail = true;
    detailStrength = 0.6f;
    detailWidth = scalerState.scaledWidth;
    detailHeight = scalerState.scaledHeight;
  }

  if (mUniformOutputSize >= 0) {
    mApi.glUniform2f(mUniformOutputSize, static_cast<float>(surfaceWidth),
                     static_cast<float>(surfaceHeight));
  }
  if (mUniformSourceSize >= 0) {
    mApi.glUniform2f(mUniformSourceSize, static_cast<float>(surfaceWidth),
                     static_cast<float>(surfaceHeight));
  }
  if (mUniformDetailSize >= 0) {
    mApi.glUniform2f(mUniformDetailSize, static_cast<float>(detailWidth),
                     static_cast<float>(detailHeight));
  }
  if (mUniformSharpness >= 0) {
    mApi.glUniform1f(mUniformSharpness, mSettings.sharpening / 100.0f);
  }
  if (mUniformDetailStrength >= 0) {
    mApi.glUniform1f(mUniformDetailStrength, detailStrength);
  }
  if (mUniformUseDetail >= 0) {
    mApi.glUniform1i(mUniformUseDetail, useDetail ? 1 : 0);
  }

  mApi.glDrawArrays(GL_TRIANGLES, 0, 3);

  if (mTimerQueryActive) {
    mApi.glEndQuery(GL_TIME_ELAPSED);
    mTimerQueryActive = false;
    ++mTimerQueriesPending;
  }

  // 3. Restore the state of the game.
  mApi.glBindFramebuffer(GL_FRAMEBUFFER,
                         static_cast<GLuint>(previousFramebuffer));
  mApi.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  mApi.glUseProgram(static_cast<GLuint>(previousProgram));
  if (mVertexArray != 0 && previousVertexArray != 0) {
    mApi.glBindVertexArray(static_cast<GLuint>(previousVertexArray));
  }
  mApi.glActiveTexture(static_cast<GLenum>(previousActiveTexture));
  mApi.glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
  if (blend) {
    mApi.glEnable(GL_BLEND);
  }
  if (depthTest) {
    mApi.glEnable(GL_DEPTH_TEST);
  }
  if (cullFace) {
    mApi.glEnable(GL_CULL_FACE);
  }
  if (scissorTest) {
    mApi.glEnable(GL_SCISSOR_TEST);
  }

  if (mSettings.gpuTimings) {
    mReconstructionCostMs =
        static_cast<double>(monotonicNanoseconds() - start) / 1000000.0;
  }
}

// Reads the result of the last completed timer query.
//
// The availability flag is polled instead of waiting for the result: a
// blocking glGetQueryObjectuiv would stall the render thread, which is
// exactly the latency this module exists to avoid. Until a result is
// available the module reports no GPU time rather than an estimate.
void RenderPipeline::collectGpuTiming() {
  if (!mTimerQuerySupported || mTimerQuery == 0 || mTimerQueriesPending == 0 ||
      mApi.glGetQueryObjectuiv == nullptr) {
    mGpuTimingValid.store(false, std::memory_order_relaxed);
    return;
  }

  GLuint available = 0;
  mApi.glGetQueryObjectuiv(mTimerQuery, GL_QUERY_RESULT_AVAILABLE, &available);
  if (available == GL_FALSE) {
    // The GPU has not finished the previous pass yet; keep the last value
    // only if it is still the same measurement window.
    return;
  }

  GLuint disjoint = 0;
  mApi.glGetQueryObjectuiv(mTimerQuery, GL_GPU_DISJOINT_EXT, &disjoint);
  GLuint nanoseconds = 0;
  mApi.glGetQueryObjectuiv(mTimerQuery, GL_QUERY_RESULT, &nanoseconds);

  --mTimerQueriesPending;
  if (disjoint != GL_FALSE) {
    // The GPU was interrupted (context switch, thermal event); the timing is
    // meaningless and must not be reported.
    mGpuTimingValid.store(false, std::memory_order_relaxed);
    return;
  }

  mGpuTimeMs.store(static_cast<double>(nanoseconds) / 1000000.0,
                   std::memory_order_relaxed);
  mGpuTimingValid.store(true, std::memory_order_relaxed);
}

void RenderPipeline::applySettings(const RuntimeSettings &settings) {
  {
    std::lock_guard<std::mutex> lock(mSettingsMutex);
    mPendingSettings = settings;
  }
  mSettingsDirty.store(true, std::memory_order_release);
}

Metrics RenderPipeline::metrics() const {
  Metrics result;
  result.fps = mFps.load(std::memory_order_relaxed);
  result.frameTimeMs = mFrameTimeMs.load(std::memory_order_relaxed);
  result.gpuTimeMs = mGpuTimeMs.load(std::memory_order_relaxed);
  result.gpuTimingValid = mGpuTimingValid.load(std::memory_order_relaxed);
  result.reconstructionCostMs =
      mReconstructionCostMs.load(std::memory_order_relaxed);
  result.frames = mFrameCounter.load(std::memory_order_relaxed);
  result.swapsObserved = mSwapCounter.load(std::memory_order_relaxed);
  result.surfaceWidth = mSurfaceWidth.load(std::memory_order_relaxed);
  result.surfaceHeight = mSurfaceHeight.load(std::memory_order_relaxed);
  const auto scalerState = mScaler.state();
  result.sceneWidth = scalerState.scaledWidth;
  result.sceneHeight = scalerState.scaledHeight;
  result.effectiveScale = mDynamicResolution.currentScale();
  result.targetFps = mSettings.targetFps;
  {
    std::lock_guard<std::mutex> lock(mStatusMutex);
    result.path = mPath;
    result.fallbackReason = mFallbackReason;
  }
  result.dynamicResolutionActive = mSettings.dynamicResolution &&
                                   scalerState.applied;
  result.internalResolutionActive = scalerState.applied;
  result.reconstructionActive = result.path == ActivePath::Reconstruction ||
                                result.path == ActivePath::InternalResolution;
  result.status = statusLine();
  return result;
}

void RenderPipeline::setFrameCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mFrameCallbackMutex);
  mFrameCallback = std::move(callback);
}

std::string RenderPipeline::statusLine() const {
  std::lock_guard<std::mutex> lock(mStatusMutex);
  switch (mPath) {
  case ActivePath::Disabled:
    return "Disabled";
  case ActivePath::Native:
    return mFallbackReason.empty() ? "Native rendering" : mFallbackReason;
  case ActivePath::Reconstruction:
    return "Reconstruction only";
  case ActivePath::InternalResolution:
    return "Reduced internal resolution + reconstruction";
  }
  return "Unknown";
}

} // namespace levifsr
