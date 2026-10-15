/**
 * LeviFSR AI - host build stub of <jni.h>.
 *
 * pl/Mod.hpp includes <jni.h> for the JavaVM pointer it stores. The Android
 * build uses the real NDK header. This stub only declares the opaque types so
 * that the host compile verification and the unit tests can include the SDK
 * headers without an Android NDK. It is never used on the device.
 */
#ifndef LEVIFSR_JNI_STUB_H
#define LEVIFSR_JNI_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t jint;
typedef int64_t jlong;
typedef int8_t jbyte;
typedef uint16_t jchar;
typedef int16_t jshort;
typedef float jfloat;
typedef double jdouble;
typedef unsigned char jboolean;

struct _jobject;
typedef struct _jobject *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;
typedef jobject jthrowable;

struct JNINativeInterface_;
struct JNIInvokeInterface_;

typedef const struct JNINativeInterface_ *JNIEnv;
typedef const struct JNIInvokeInterface_ *JavaVM;

struct JNIInvokeInterface_;

#ifdef __cplusplus
}
#endif

#endif // LEVIFSR_JNI_STUB_H
