/**
 * LeviFSR AI - host build stub of <EGL/egl.h>.
 *
 * The Android build links the real Khronos EGL header from
 * third_party/khronos. This stub only declares the subset the module uses so
 * that the host unit tests can compile without an Android NDK. It is never
 * used on the device.
 */
#ifndef LEVIFSR_EGL_STUB_H
#define LEVIFSR_EGL_STUB_H

#include <sys/types.h>

typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef int EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;

#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)

#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_READ 0x305A
#define EGL_DRAW 0x3059
#define EGL_TRUE 1
#define EGL_FALSE 0

#ifdef __cplusplus
extern "C" {
#endif

EGLDisplay eglGetCurrentDisplay(void);
EGLSurface eglGetCurrentSurface(EGLint);
EGLContext eglGetCurrentContext(void);
EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface);
EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay, EGLSurface, const EGLint *,
                                       EGLint);
EGLBoolean eglQuerySurface(EGLDisplay, EGLSurface, EGLint, EGLint *);

#ifdef __cplusplus
}
#endif

#endif // LEVIFSR_EGL_STUB_H
