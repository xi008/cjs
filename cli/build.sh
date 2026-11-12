#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

STUB="$ROOT/cli/stubs"
COMMON="-O3 -std=gnu99 -fvisibility=hidden -D_GNU_SOURCE"
FLG="-DNDK_DEBUG -DWASM_RT_USE_MMAP=0"

CCTV_SRC="cli/cctv_main.c sites/tv.cctv.com/native/generated/cctv_h5e_wasm.c sites/tv.cctv.com/native/wasm-rt/wasm-rt-impl.c sites/tv.cctv.com/native/wasm-rt/wasm-rt-mem-impl.c"
CCTV_INC="-I$STUB -Isites/tv.cctv.com/native -Isites/tv.cctv.com/native/generated -Isites/tv.cctv.com/native/wasm-rt"

YSP_SRC="cli/ysp_main.c sites/yangshipin.cn/native/generated_ysp/ysp_keygen_wasm.c sites/yangshipin.cn/native/ysp-rt/wasm-rt-impl.c"
YSP_INC="-I$STUB -Isites/yangshipin.cn/native -Isites/yangshipin.cn/native/generated_ysp -Isites/yangshipin.cn/native/ysp-rt"

find_ndk() {
  if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then echo "$ANDROID_NDK_HOME"; return; fi
  if [ -n "${ANDROID_NDK_ROOT:-}" ] && [ -d "$ANDROID_NDK_ROOT" ]; then echo "$ANDROID_NDK_ROOT"; return; fi
  local d
  for d in /usr/local/lib/android/sdk/ndk/* "$HOME"/Android/Sdk/ndk/*; do
    [ -d "$d" ] && { echo "$d"; return; }
  done
  return 1
}

build_host() {
  local CC LIBCURL
  CC=$(command -v gcc || command -v clang)
  if pkg-config --exists libcurl 2>/dev/null; then LIBCURL=$(pkg-config --libs libcurl); else LIBCURL="-lcurl"; fi
  mkdir -p dist/host
  echo "[host] ysp_cli"
  $CC $COMMON $FLG $YSP_INC -pthread -o dist/host/ysp_cli $YSP_SRC -lm
  echo "[host] h5e_cli"
  $CC $COMMON $FLG $CCTV_INC -o dist/host/h5e_cli $CCTV_SRC -lm $LIBCURL
}

build_android() {
  local NDK
  NDK=$(find_ndk) || { echo "NDK not found" >&2; exit 1; }
  local PREBUILT="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
  local DEPS="$ROOT/cli/deps/out"
  local abi triple out CC EXTRA CURLLIBS
  for spec in "armeabi-v7a:armv7a-linux-androideabi" "arm64-v8a:aarch64-linux-android"; do
    abi=${spec%%:*}
    triple=${spec##*:}
    CC="$PREBUILT/bin/${triple}21-clang"
    if [ -f "$DEPS/$abi/libcurl.a" ]; then
      EXTRA="-I$DEPS/$abi/include"
      CURLLIBS="$DEPS/$abi/libcurl.a $DEPS/$abi/libssl.a $DEPS/$abi/libcrypto.a"
    else
      echo "deps missing for $abi, run cli/deps/build_https.sh first" >&2
      exit 1
    fi
    out="dist/$abi"
    mkdir -p "$out"
    echo "[$abi] ysp_cli"
    $CC $COMMON $FLG $YSP_INC $EXTRA -o "$out/ysp_cli" $YSP_SRC -lm
    echo "[$abi] h5e_cli"
    $CC $COMMON $FLG $CCTV_INC $EXTRA -o "$out/h5e_cli" $CCTV_SRC -lm $CURLLIBS
  done
}

case "${1:-all}" in
  host) build_host ;;
  android) build_android ;;
  all) build_host; build_android ;;
  *) echo "usage: build.sh [host|android|all]" >&2; exit 2 ;;
esac