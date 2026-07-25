#include "../../ra_client_zelda3.h"

#include <jni.h>

static const char *JniGetString(JNIEnv *env, jstring value) {
  return value ? (*env)->GetStringUTFChars(env, value, NULL) : NULL;
}

JNIEXPORT void JNICALL
Java_com_dishii_zelda3_RetroAchievementsBridge_nativeConfigure(
    JNIEnv *env, jclass clazz, jboolean enabled, jboolean spectator,
    jstring client_name, jstring client_version, jstring username, jstring secret,
    jboolean secret_is_token) {
  const char *native_client_name = JniGetString(env, client_name);
  const char *native_client_version = JniGetString(env, client_version);
  const char *native_username = JniGetString(env, username);
  const char *native_secret = JniGetString(env, secret);

  (void)clazz;
  RaClientZelda3_Configure(enabled == JNI_TRUE, spectator == JNI_TRUE,
                           native_client_name, native_client_version,
                           native_username, native_secret,
                           secret_is_token == JNI_TRUE);
  if (native_client_name)
    (*env)->ReleaseStringUTFChars(env, client_name, native_client_name);
  if (native_client_version)
    (*env)->ReleaseStringUTFChars(env, client_version, native_client_version);
  if (native_username)
    (*env)->ReleaseStringUTFChars(env, username, native_username);
  if (native_secret)
    (*env)->ReleaseStringUTFChars(env, secret, native_secret);
}

JNIEXPORT void JNICALL
Java_com_dishii_zelda3_RetroAchievementsBridge_nativeLogout(
    JNIEnv *env, jclass clazz) {
  (void)env;
  (void)clazz;
  RaClientZelda3_RequestLogout();
}

JNIEXPORT jstring JNICALL
Java_com_dishii_zelda3_RetroAchievementsBridge_nativeSnapshot(
    JNIEnv *env, jclass clazz) {
  char snapshot[512];

  (void)clazz;
  RaClientZelda3_Snapshot(snapshot, sizeof(snapshot));
  return (*env)->NewStringUTF(env, snapshot);
}
