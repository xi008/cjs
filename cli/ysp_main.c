#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#include "../sites/yangshipin.cn/native/ysp_keygen.c"

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  (void)prio;
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[%s] ", tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  return 0;
}

static jsize stub_GetArrayLength(JNIEnv *env, jbyteArray a) {
  (void)env; (void)a; return 0;
}

static jbyte *stub_GetByteArrayElements(JNIEnv *env, jbyteArray a, jboolean *c) {
  (void)env; (void)a; (void)c; return NULL;
}

static void stub_ReleaseByteArrayElements(JNIEnv *env, jbyteArray a, jbyte *p, jint m) {
  (void)env; (void)a; (void)p; (void)m;
}

static const char *stub_GetStringUTFChars(JNIEnv *env, jstring s, jboolean *c) {
  (void)env; (void)c; return s ? s : "";
}

static void stub_ReleaseStringUTFChars(JNIEnv *env, jstring s, const char *c) {
  (void)env; (void)s; (void)c;
}

static jstring stub_NewStringUTF(JNIEnv *env, const char *s) {
  (void)env; return s;
}

static const struct JNINativeInterface stub_table = {
    .GetArrayLength = stub_GetArrayLength,
    .GetByteArrayElements = stub_GetByteArrayElements,
    .ReleaseByteArrayElements = stub_ReleaseByteArrayElements,
    .GetStringUTFChars = stub_GetStringUTFChars,
    .ReleaseStringUTFChars = stub_ReleaseStringUTFChars,
    .NewStringUTF = stub_NewStringUTF,
};

static JNIEnv stub_env = &stub_table;

int main(int argc, char **argv) {
  if (argc == 4 && strcmp(argv[1], "token-rnd") == 0) {
    const char *out = Java_com_bu_cc_tv_NativeYspSigner_tokenRnd(&stub_env, NULL, argv[2], argv[3]);
    printf("%s\n", out ? out : "");
    return out && *out ? 0 : 1;
  }
  if (argc == 5 && strcmp(argv[1], "sign") == 0) {
    const char *out = Java_com_bu_cc_tv_NativeYspSigner_signature(&stub_env, NULL, argv[2], argv[3], argv[4]);
    printf("%s\n", out ? out : "");
    return out && *out ? 0 : 1;
  }
  fprintf(stderr, "usage: ysp_cli token-rnd <guid> <timestamp>\n       ysp_cli sign <guid> <token> <input>\n");
  return 2;
}