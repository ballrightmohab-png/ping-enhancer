#include "GlApi.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace levifsr::gfx {
namespace {

template <typename Fn>
Fn resolve(EglGetProcAddressFn proc, const char *name) {
  if (proc != nullptr) {
    auto symbol = proc(name);
    if (symbol != nullptr) {
      return reinterpret_cast<Fn>(symbol);
    }
  }
  return reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, name));
}

template <typename Fn> Fn resolveSymbol(const char *name) {
  return reinterpret_cast<Fn>(dlsym(RTLD_DEFAULT, name));
}

std::string toLower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

std::vector<std::string> splitExtensions(const char *raw) {
  std::vector<std::string> result;
  if (raw == nullptr) {
    return result;
  }
  std::istringstream stream(raw);
  std::string token;
  while (stream >> token) {
    result.push_back(token);
  }
  return result;
}

} // namespace

bool loadApi(Api &api, EglGetProcAddressFn eglProc) {
  api.eglGetProcAddress = eglProc;

  api.eglGetCurrentDisplay = resolveSymbol<decltype(api.eglGetCurrentDisplay)>(
      "eglGetCurrentDisplay");
  api.eglGetCurrentSurface = resolveSymbol<decltype(api.eglGetCurrentSurface)>(
      "eglGetCurrentSurface");
  api.eglGetCurrentContext = resolveSymbol<decltype(api.eglGetCurrentContext)>(
      "eglGetCurrentContext");
  api.eglSwapBuffers =
      resolveSymbol<decltype(api.eglSwapBuffers)>("eglSwapBuffers");
  api.eglSwapBuffersWithDamageKHR =
      resolve<decltype(api.eglSwapBuffersWithDamageKHR)>(
          eglProc, "eglSwapBuffersWithDamageKHR");
  api.eglQuerySurface =
      resolveSymbol<decltype(api.eglQuerySurface)>("eglQuerySurface");
  api.eglQueryString =
      resolveSymbol<decltype(api.eglQueryString)>("eglQueryString");
  api.eglGetError = resolveSymbol<decltype(api.eglGetError)>("eglGetError");

  if (api.eglGetCurrentContext == nullptr || api.eglSwapBuffers == nullptr) {
    api.complete = false;
    return false;
  }

  api.glGetString = resolve<decltype(api.glGetString)>(eglProc, "glGetString");
  api.glGetStringi = resolve<decltype(api.glGetStringi)>(eglProc, "glGetStringi");
  api.glGetIntegerv = resolve<decltype(api.glGetIntegerv)>(eglProc, "glGetIntegerv");
  api.glGetError = resolve<decltype(api.glGetError)>(eglProc, "glGetError");
  api.glEnable = resolve<decltype(api.glEnable)>(eglProc, "glEnable");
  api.glDisable = resolve<decltype(api.glDisable)>(eglProc, "glDisable");
  api.glIsEnabled = resolve<decltype(api.glIsEnabled)>(eglProc, "glIsEnabled");
  api.glViewport = resolve<decltype(api.glViewport)>(eglProc, "glViewport");
  api.glScissor = resolve<decltype(api.glScissor)>(eglProc, "glScissor");
  api.glClearColor = resolve<decltype(api.glClearColor)>(eglProc, "glClearColor");
  api.glClear = resolve<decltype(api.glClear)>(eglProc, "glClear");
  api.glDrawArrays = resolve<decltype(api.glDrawArrays)>(eglProc, "glDrawArrays");

  api.glGenTextures = resolve<decltype(api.glGenTextures)>(eglProc, "glGenTextures");
  api.glDeleteTextures =
      resolve<decltype(api.glDeleteTextures)>(eglProc, "glDeleteTextures");
  api.glGenerateMipmap =
      resolve<decltype(api.glGenerateMipmap)>(eglProc, "glGenerateMipmap");
  api.glBindTexture = resolve<decltype(api.glBindTexture)>(eglProc, "glBindTexture");
  api.glTexImage2D = resolve<decltype(api.glTexImage2D)>(eglProc, "glTexImage2D");
  api.glTexStorage2D = resolve<decltype(api.glTexStorage2D)>(eglProc, "glTexStorage2D");
  api.glTexParameteri =
      resolve<decltype(api.glTexParameteri)>(eglProc, "glTexParameteri");
  api.glActiveTexture = resolve<decltype(api.glActiveTexture)>(eglProc, "glActiveTexture");
  api.glPixelStorei = resolve<decltype(api.glPixelStorei)>(eglProc, "glPixelStorei");

  api.glGenFramebuffers =
      resolve<decltype(api.glGenFramebuffers)>(eglProc, "glGenFramebuffers");
  api.glDeleteFramebuffers =
      resolve<decltype(api.glDeleteFramebuffers)>(eglProc, "glDeleteFramebuffers");
  api.glBindFramebuffer =
      resolve<decltype(api.glBindFramebuffer)>(eglProc, "glBindFramebuffer");
  api.glFramebufferTexture2D = resolve<decltype(api.glFramebufferTexture2D)>(
      eglProc, "glFramebufferTexture2D");
  api.glFramebufferRenderbuffer = resolve<decltype(api.glFramebufferRenderbuffer)>(
      eglProc, "glFramebufferRenderbuffer");
  api.glCheckFramebufferStatus = resolve<decltype(api.glCheckFramebufferStatus)>(
      eglProc, "glCheckFramebufferStatus");
  api.glBlitFramebuffer = resolve<decltype(api.glBlitFramebuffer)>(
      eglProc, "glBlitFramebuffer");

  api.glGenRenderbuffers = resolve<decltype(api.glGenRenderbuffers)>(
      eglProc, "glGenRenderbuffers");
  api.glDeleteRenderbuffers = resolve<decltype(api.glDeleteRenderbuffers)>(
      eglProc, "glDeleteRenderbuffers");
  api.glBindRenderbuffer = resolve<decltype(api.glBindRenderbuffer)>(
      eglProc, "glBindRenderbuffer");
  api.glRenderbufferStorage = resolve<decltype(api.glRenderbufferStorage)>(
      eglProc, "glRenderbufferStorage");
  api.glRenderbufferStorageMultisample =
      resolve<decltype(api.glRenderbufferStorageMultisample)>(
          eglProc, "glRenderbufferStorageMultisample");

  api.glGenVertexArrays =
      resolve<decltype(api.glGenVertexArrays)>(eglProc, "glGenVertexArrays");
  api.glDeleteVertexArrays =
      resolve<decltype(api.glDeleteVertexArrays)>(eglProc, "glDeleteVertexArrays");
  api.glBindVertexArray =
      resolve<decltype(api.glBindVertexArray)>(eglProc, "glBindVertexArray");

  api.glCreateShader = resolve<decltype(api.glCreateShader)>(eglProc, "glCreateShader");
  api.glShaderSource = resolve<decltype(api.glShaderSource)>(eglProc, "glShaderSource");
  api.glCompileShader = resolve<decltype(api.glCompileShader)>(eglProc, "glCompileShader");
  api.glGetShaderiv = resolve<decltype(api.glGetShaderiv)>(eglProc, "glGetShaderiv");
  api.glGetShaderInfoLog =
      resolve<decltype(api.glGetShaderInfoLog)>(eglProc, "glGetShaderInfoLog");
  api.glDeleteShader = resolve<decltype(api.glDeleteShader)>(eglProc, "glDeleteShader");
  api.glCreateProgram = resolve<decltype(api.glCreateProgram)>(eglProc, "glCreateProgram");
  api.glAttachShader = resolve<decltype(api.glAttachShader)>(eglProc, "glAttachShader");
  api.glLinkProgram = resolve<decltype(api.glLinkProgram)>(eglProc, "glLinkProgram");
  api.glGetProgramiv = resolve<decltype(api.glGetProgramiv)>(eglProc, "glGetProgramiv");
  api.glGetProgramInfoLog =
      resolve<decltype(api.glGetProgramInfoLog)>(eglProc, "glGetProgramInfoLog");
  api.glUseProgram = resolve<decltype(api.glUseProgram)>(eglProc, "glUseProgram");
  api.glDeleteProgram = resolve<decltype(api.glDeleteProgram)>(eglProc, "glDeleteProgram");
  api.glGetUniformLocation =
      resolve<decltype(api.glGetUniformLocation)>(eglProc, "glGetUniformLocation");
  api.glUniform1i = resolve<decltype(api.glUniform1i)>(eglProc, "glUniform1i");
  api.glUniform1f = resolve<decltype(api.glUniform1f)>(eglProc, "glUniform1f");
  api.glUniform2f = resolve<decltype(api.glUniform2f)>(eglProc, "glUniform2f");
  api.glUniform4f = resolve<decltype(api.glUniform4f)>(eglProc, "glUniform4f");

  api.glReadPixels = resolve<decltype(api.glReadPixels)>(eglProc, "glReadPixels");
  api.glFinish = resolve<decltype(api.glFinish)>(eglProc, "glFinish");
  api.glFlush = resolve<decltype(api.glFlush)>(eglProc, "glFlush");

  api.glBeginQuery = resolve<decltype(api.glBeginQuery)>(eglProc, "glBeginQuery");
  api.glEndQuery = resolve<decltype(api.glEndQuery)>(eglProc, "glEndQuery");
  api.glGetQueryObjectuiv =
      resolve<decltype(api.glGetQueryObjectuiv)>(eglProc, "glGetQueryObjectuiv");
  api.glGenQueries = resolve<decltype(api.glGenQueries)>(eglProc, "glGenQueries");
  api.glDeleteQueries =
      resolve<decltype(api.glDeleteQueries)>(eglProc, "glDeleteQueries");

  api.complete = api.glGetString != nullptr && api.glGetIntegerv != nullptr &&
                 api.glBindFramebuffer != nullptr && api.glGenTextures != nullptr &&
                 api.glCreateProgram != nullptr && api.glDrawArrays != nullptr;

  return api.complete;
}

GpuFamily classifyRenderer(std::string_view renderer, std::string_view vendor) {
  const std::string haystack = toLower(renderer) + " " + toLower(vendor);
  if (haystack.find("mali") != std::string::npos ||
      haystack.find("arm ltd") != std::string::npos ||
      haystack.find("utgard") != std::string::npos) {
    return GpuFamily::Mali;
  }
  if (haystack.find("adreno") != std::string::npos ||
      haystack.find("qualcomm") != std::string::npos) {
    return GpuFamily::Adreno;
  }
  if (haystack.find("powervr") != std::string::npos ||
      haystack.find("imagination") != std::string::npos) {
    return GpuFamily::PowerVr;
  }
  if (haystack.find("intel") != std::string::npos) {
    return GpuFamily::Intel;
  }
  if (!renderer.empty() || !vendor.empty()) {
    return GpuFamily::Other;
  }
  return GpuFamily::Unknown;
}

bool GpuInfo::hasExtension(std::string_view name) const {
  return std::find(glExtensions.begin(), glExtensions.end(), name) !=
         glExtensions.end();
}

std::string GpuInfo::familyLabel() const {
  switch (family) {
  case GpuFamily::Mali:
    return "ARM Mali";
  case GpuFamily::Adreno:
    return "Qualcomm Adreno";
  case GpuFamily::PowerVr:
    return "PowerVR";
  case GpuFamily::Intel:
    return "Intel";
  case GpuFamily::Other:
    return "Other";
  case GpuFamily::Unknown:
    break;
  }
  return "Unknown";
}

std::string GpuInfo::backendLabel() const {
  switch (backend) {
  case GraphicsBackend::OpenGLES:
    return "OpenGL ES";
  case GraphicsBackend::Vulkan:
    return "Vulkan";
  case GraphicsBackend::Unknown:
    break;
  }
  return "Unknown";
}

GpuInfo detectCapabilities(const Api &api) {
  GpuInfo info;

  if (api.eglGetCurrentDisplay != nullptr) {
    auto display = api.eglGetCurrentDisplay();
    if (display != EGL_NO_DISPLAY) {
      if (api.eglQueryString != nullptr) {
        const char *vendor = api.eglQueryString(display, EGL_VENDOR);
        const char *version = api.eglQueryString(display, EGL_VERSION);
        const char *extensions = api.eglQueryString(display, EGL_EXTENSIONS);
        info.eglVendor = vendor != nullptr ? vendor : "";
        info.eglVersion = version != nullptr ? version : "";
        info.eglExtensions = extensions != nullptr ? extensions : "";
      }
    }
  }

  if (api.glGetString == nullptr) {
    return info;
  }

  const auto *vendorRaw = api.glGetString(GL_VENDOR);
  const auto *rendererRaw = api.glGetString(GL_RENDERER);
  const auto *versionRaw = api.glGetString(GL_VERSION);
  const auto *glslRaw = api.glGetString(GL_SHADING_LANGUAGE_VERSION);
  info.vendor = vendorRaw != nullptr ? reinterpret_cast<const char *>(vendorRaw) : "";
  info.renderer =
      rendererRaw != nullptr ? reinterpret_cast<const char *>(rendererRaw) : "";
  info.version = versionRaw != nullptr ? reinterpret_cast<const char *>(versionRaw) : "";
  info.glslVersion = glslRaw != nullptr ? reinterpret_cast<const char *>(glslRaw) : "";

  if (api.glGetIntegerv != nullptr) {
    GLint major = 0;
    GLint minor = 0;
    api.glGetIntegerv(GL_MAJOR_VERSION, &major);
    api.glGetIntegerv(GL_MINOR_VERSION, &minor);
    info.glesMajor = major;
    info.glesMinor = minor;

    GLint maxTexture = 0;
    api.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    info.maxTextureSize = maxTexture;
  }

  if (api.glGetStringi != nullptr) {
    GLint count = 0;
    if (api.glGetIntegerv != nullptr) {
      api.glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    }
    for (GLint i = 0; i < count; ++i) {
      const auto *raw = api.glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
      if (raw != nullptr) {
        info.glExtensions.emplace_back(reinterpret_cast<const char *>(raw));
      }
    }
  }

  info.family = classifyRenderer(info.renderer, info.vendor);
  info.hasTimerQuery = info.hasExtension("GL_EXT_disjoint_timer_query");
  info.hasAnisotropicFiltering =
      info.hasExtension("GL_EXT_texture_filter_anisotropic");

  const std::string eglExtensionsLower = toLower(info.eglExtensions);
  info.hasDamageSwap = eglExtensionsLower.find("egl_khr_swap_buffers_with_damage") !=
                           std::string::npos ||
                       eglExtensionsLower.find("egl_ext_swap_buffers_with_damage") !=
                           std::string::npos ||
                       info.eglVersion.find("1.5") != std::string::npos;

  // A current EGL context proves that the game is presenting through EGL.
  if (api.eglGetCurrentContext != nullptr &&
      api.eglGetCurrentContext() != EGL_NO_CONTEXT) {
    info.backend = GraphicsBackend::OpenGLES;
  } else if (info.vulkanLoaderPresent) {
    info.backend = GraphicsBackend::Vulkan;
  }

  return info;
}

} // namespace levifsr::gfx
