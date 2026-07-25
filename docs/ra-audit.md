# RetroAchievements Build Checkpoint

## Vendored upstream

`app/jni/src/third_party/rcheevos` is the official rcheevos `v12.4.0`
snapshot at commit `2ad0b8672f68a48148620164510b963039e49eb1`. The upstream MIT
text is byte-for-byte in `third_party/rcheevos/LICENSE`; `VERSION` records
the tag, commit, source repository, and license.

## Audited insertion points

- `app/jni/src/Android.mk`: `LOCAL_C_INCLUDES` adds the public rcheevos
  headers. `RCHEEVOS_CLIENT_SRC` is an explicit ndk-build list for the
  rc_client path, then `LOCAL_SRC_FILES` appends that list.
- `app/jni/src/Makefile`: `RCHEEVOS_CLIENT_SRCS` mirrors that same source
  set and adds the public headers, so the existing Linux executable still
  links with the vendored client.
- `app/build.gradle`: Android Gradle Plugin 7.0.3 is pinned to NDK
  `25.2.9519653` and builds `arm64-v8a`.
- `app/jni/src/src/ra_client_zelda3.*`, `ra_memory.*`, `ra_http.*`, and
  `ra_state.*`: future integration boundaries. They are compiled but have
  no callers, no network implementation, no login, no frame hook, no UI,
  and no submission behavior. Their platform-neutral stubs keep the Linux
  build linkable as well as Android.

The explicit rcheevos list includes `rc_client.c`, compatibility/util/version,
the required `rapi` and runtime parser sources, and `rhash/md5.c`. It does
not define `RC_CLIENT_SUPPORTS_HASH`, and it intentionally omits the hash
backend, libretro integration, RAIntegration bridge, and external-client
adapter.

## Toolchain and ABI

The verified target is `arm64-v8a` using NDK `25.2.9519653`, matching the
Apple-silicon Android build environment. Gradle debug builds no longer
package the prior `armeabi-v7a`, `x86`, or `x86_64` variants. `Application.mk`
keeps its legacy ABI list for direct/manual ndk-build users; restoring
multi-ABI Gradle packages requires validating those ABIs with this vendored
source set before expanding `abiFilters`.
