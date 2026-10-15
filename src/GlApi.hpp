// LeviFSR AI - dynamic OpenGL ES / EGL entry point table and GPU detection.
//
// The module never links against libGLESv3/libEGL directly. Every entry point
// is resolved at runtime from the context that is already current inside the
// Minecraft process, which keeps the module loadable even when the game runs
// on a different graphics stack (for example Vulkan) and lets us report an
// honest "unsupported backend" instead of crashing.

#pragma once

#include "Config.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

namespace levifsr::gfx {

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

struct Api {
  // EGL ---------------------------------------------------------------
  EGLDisplay (*eglGetCurrentDisplay)(void) = nullptr;
  EGLSurface (*eglGetCurrentSurface)(EGLint) = nullptr;
  EGLContext (*eglGetCurrentContext)(void) = nullptr;
  EGLBoolean (*eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
  EGLBoolean (*eglSwapBuffersWithDamageKHR)(EGLDisplay, EGLSurface,
                                            const EGLint *, EGLint) = nullptr;
  EGLBoolean (*eglQuerySurface)(EGLDisplay, EGLSurface, EGLint,
                                EGLint *) = nullptr;
  const char *(*eglQueryString)(EGLDisplay, EGLint) = nullptr;
  EGLint (*eglGetError)(void) = nullptr;
  void *(*eglGetProcAddress)(const char *) = nullptr;

  // GLES3 -------------------------------------------------------------
  const GLubyte *(*glGetString)(GLenum) = nullptr;
  const GLubyte *(*glGetStringi)(GLenum, GLuint) = nullptr;
  void (*glGetIntegerv)(GLenum, GLint *) = nullptr;
  GLenum (*glGetError)(void) = nullptr;
  void (*glEnable)(GLenum) = nullptr;
  void (*glDisable)(GLenum) = nullptr;
  GLboolean (*glIsEnabled)(GLenum) = nullptr;
  void (*glViewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
  void (*glScissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
  void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
  void (*glClear)(GLbitfield) = nullptr;
  void (*glDrawArrays)(GLenum, GLint, GLsizei) = nullptr;

  void (*glGenTextures)(GLsizei, GLuint *) = nullptr;
  void (*glDeleteTextures)(GLsizei, const GLuint *) = nullptr;
  void (*glBindTexture)(GLenum, GLuint) = nullptr;
  void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                       GLenum, const void *) = nullptr;
  void (*glTexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei) = nullptr;
  void (*glTexParameteri)(GLenum, GLenum, GLint) = nullptr;
  void (*glGenerateMipmap)(GLenum) = nullptr;
  void (*glActiveTexture)(GLenum) = nullptr;
  void (*glPixelStorei)(GLenum, GLint) = nullptr;

  void (*glGenFramebuffers)(GLsizei, GLuint *) = nullptr;
  void (*glDeleteFramebuffers)(GLsizei, const GLuint *) = nullptr;
  void (*glBindFramebuffer)(GLenum, GLuint) = nullptr;
  void (*glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
  void (*glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
  GLenum (*glCheckFramebufferStatus)(GLenum) = nullptr;
  void (*glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                            GLint, GLbitfield, GLenum) = nullptr;

  void (*glGenRenderbuffers)(GLsizei, GLuint *) = nullptr;
  void (*glDeleteRenderbuffers)(GLsizei, const GLuint *) = nullptr;
  void (*glBindRenderbuffer)(GLenum, GLuint) = nullptr;
  void (*glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
  void (*glRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei,
                                            GLsizei) = nullptr;

  void (*glGenVertexArrays)(GLsizei, GLuint *) = nullptr;
  void (*glDeleteVertexArrays)(GLsizei, const GLuint *) = nullptr;
  void (*glBindVertexArray)(GLuint) = nullptr;

  GLuint (*glCreateShader)(GLenum) = nullptr;
  void (*glShaderSource)(GLuint, GLsizei, const GLchar *const *,
                         const GLint *) = nullptr;
  void (*glCompileShader)(GLuint) = nullptr;
  void (*glGetShaderiv)(GLuint, GLenum, GLint *) = nullptr;
  void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *) = nullptr;
  void (*glDeleteShader)(GLuint) = nullptr;
  GLuint (*glCreateProgram)(void) = nullptr;
  void (*glAttachShader)(GLuint, GLuint) = nullptr;
  void (*glLinkProgram)(GLuint) = nullptr;
  void (*glGetProgramiv)(GLuint, GLenum, GLint *) = nullptr;
  void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *) = nullptr;
  void (*glUseProgram)(GLuint) = nullptr;
  void (*glDeleteProgram)(GLuint) = nullptr;
  GLint (*glGetUniformLocation)(GLuint, const GLchar *) = nullptr;
  void (*glUniform1i)(GLint, GLint) = nullptr;
  void (*glUniform1f)(GLint, GLfloat) = nullptr;
  void (*glUniform2f)(GLint, GLfloat, GLfloat) = nullptr;
  void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;

  void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                       void *) = nullptr;
  void (*glFinish)(void) = nullptr;
  void (*glFlush)(void) = nullptr;

  // GL_EXT_disjoint_timer_query (optional, Mali and Adreno both expose it).
  void (*glBeginQuery)(GLenum, GLuint) = nullptr;
  void (*glEndQuery)(GLenum) = nullptr;
  void (*glGetQueryObjectuiv)(GLuint, GLenum, GLuint *) = nullptr;
  void (*glGenQueries)(GLsizei, GLuint *) = nullptr;
  void (*glDeleteQueries)(GLsizei, const GLuint *) = nullptr;

  // True when every mandatory entry point could be resolved.
  bool complete = false;
};

// Type of eglGetProcAddress. The Khronos headers declare it with the
// __eglMustCastToProperFunctionPointerType typedef, which is only available
// when the EGL platform header has been included; a plain void* resolver has
// the same ABI and keeps this header usable on its own.
using EglGetProcAddressFn = void *(*)(const char *);

// Resolves the entry points from the currently bound EGL/GL context.
// `eglProc` is the eglGetProcAddress of the active display; it may be null, in
// which case dlsym() is used exclusively.
bool loadApi(Api &api, EglGetProcAddressFn eglProc);

// ---------------------------------------------------------------------------
// Capability detection
// ---------------------------------------------------------------------------

struct GpuInfo {
  GraphicsBackend backend = GraphicsBackend::Unknown;
  GpuFamily family = GpuFamily::Unknown;
  std::string vendor;
  std::string renderer;
  std::string version;
  std::string glslVersion;
  std::string eglVendor;
  std::string eglVersion;
  std::string eglExtensions;
  std::vector<std::string> glExtensions;

  int glesMajor = 0;
  int glesMinor = 0;
  int maxTextureSize = 0;
  bool hasTimerQuery = false;
  bool hasDamageSwap = false;
  bool hasAnisotropicFiltering = false;

  // True when the process exposes a Vulkan loader; the game may still run on
  // GLES, this only tells us that a Vulkan path is possible.
  bool vulkanLoaderPresent = false;

  [[nodiscard]] std::string familyLabel() const;
  [[nodiscard]] std::string backendLabel() const;
  [[nodiscard]] bool hasExtension(std::string_view name) const;
};

// Classifies a GL_RENDERER / GL_VENDOR string into a GPU family.
GpuFamily classifyRenderer(std::string_view renderer,
                            std::string_view vendor);

GpuInfo detectCapabilities(const Api &api);

} // namespace levifsr::gfx
