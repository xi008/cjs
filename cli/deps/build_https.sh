#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/cli/deps/out"
SRC="$ROOT/cli/deps/src"
mkdir -p "$OUT" "$SRC"
cd "$SRC"

OPENSSL_VER=3.3.4
CURL_VER=8.11.1

find_ndk() {
  if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then echo "$ANDROID_NDK_HOME"; return; fi
  if [ -n "${ANDROID_NDK_ROOT:-}" ] && [ -d "$ANDROID_NDK_ROOT" ]; then echo "$ANDROID_NDK_ROOT"; return; fi
  local d
  for d in /usr/local/lib/android/sdk/ndk/* "$HOME"/Android/Sdk/ndk/*; do
    [ -d "$d" ] && { echo "$d"; return; }
  done
  return 1
}

NDK=$(find_ndk) || { echo "NDK not found" >&2; exit 1; }
PREBUILT="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
export ANDROID_NDK_ROOT="$NDK"
export ANDROID_NDK_HOME="$NDK"
export PATH="$PREBUILT/bin:$PATH"
unset CROSS_COMPILE CC AR RANLIB || true

fetch() {
  local url="$1" file="$2"
  [ -f "$file" ] || curl -fsSL -o "$file" "$url"
}

fetch "https://www.openssl.org/source/openssl-${OPENSSL_VER}.tar.gz" "openssl-${OPENSSL_VER}.tar.gz"
fetch "https://curl.se/download/curl-${CURL_VER}.tar.gz" "curl-${CURL_VER}.tar.gz"
[ -d "openssl-${OPENSSL_VER}" ] || tar xzf "openssl-${OPENSSL_VER}.tar.gz"
[ -d "curl-${CURL_VER}" ] || tar xzf "curl-${CURL_VER}.tar.gz"

build_abi() {
  local abi="$1" triple="$2" host="$3"
  local dest="$OUT/$abi"
  local cc="$PREBUILT/bin/${triple}21-clang"
  local ar="$PREBUILT/bin/llvm-ar"
  local target
  case "$abi" in
    armeabi-v7a) target="android-arm" ;;
    arm64-v8a) target="android-arm64" ;;
  esac
  if [ -f "$dest/libcurl.a" ]; then
    echo "[$abi] deps cached"
    return
  fi
  mkdir -p "$dest"

  pushd "openssl-${OPENSSL_VER}" >/dev/null
  if [ ! -f "$dest/.openssl.done" ]; then
    [ -f Makefile ] && make clean >/dev/null 2>&1 || true
    unset CC AR RANLIB || true
    perl ./Configure "$target" no-shared no-tests no-ui-console no-apps \
      -D__ANDROID_API__=21 \
      --prefix="$dest" --openssldir="$dest/ssl"
    make -j"$(nproc)" build_libs
    make install_dev
    touch "$dest/.openssl.done"
  fi
  popd >/dev/null

  pushd "curl-${CURL_VER}" >/dev/null
  if [ ! -f "$dest/.curl.done" ]; then
    [ -f Makefile ] && make distclean >/dev/null 2>&1 || true
    export CC="$cc" AR="$ar" RANLIB="$PREBUILT/bin/llvm-ranlib"
    ./configure --host="$host" --prefix="$dest" \
      --disable-shared --disable-ldap --disable-ldaps --disable-rtsp \
      --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
      --disable-imap --disable-smb --disable-smtp --disable-gopher \
      --disable-manual --disable-ntlm-wb --disable-unix-sockets \
      --without-zlib --without-brotli --without-zstd --without-libidn2 \
      --with-openssl="$dest" --without-nghttp2 --without-libpsl
    make -j"$(nproc)"
    make install
    touch "$dest/.curl.done"
  fi
  popd >/dev/null
}

build_abi armeabi-v7a armv7a-linux-androideabi arm-linux-androideabi
build_abi arm64-v8a aarch64-linux-android aarch64-linux-android
echo "deps done"