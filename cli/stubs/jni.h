#ifndef CLI_STUB_JNI_H
#define CLI_STUB_JNI_H
#include <stdint.h>
#define JNIEXPORT
#define JNICALL
#define JNI_FALSE 0
#define JNI_TRUE 1
#define JNI_ABORT 2
typedef int8_t jboolean;
typedef int8_t jbyte;
typedef int16_t jshort;
typedef int32_t jint;
typedef int64_t jlong;
typedef int32_t jsize;
typedef float jfloat;
typedef double jdouble;
typedef void *jobject;
typedef void *jclass;
typedef void *jbyteArray;
typedef const char *jstring;
struct JNINativeInterface;
typedef const struct JNINativeInterface *JNIEnv;
struct JNINativeInterface {
  jsize (*GetArrayLength)(JNIEnv *, jbyteArray);
  jbyte *(*GetByteArrayElements)(JNIEnv *, jbyteArray, jboolean *);
  void (*ReleaseByteArrayElements)(JNIEnv *, jbyteArray, jbyte *, jint);
  const char *(*GetStringUTFChars)(JNIEnv *, jstring, jboolean *);
  void (*ReleaseStringUTFChars)(JNIEnv *, jstring, const char *);
  jstring (*NewStringUTF)(JNIEnv *, const char *);
};
#endif