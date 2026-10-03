#!/usr/bin/env bash
#
# Copyright (c) 2026, Daily
#
# Builds the Pipecat C++ client and its WebSocket transport with Xcode's clang,
# which Unreal Engine uses on macOS, for Apple silicon and Intel Macs, and
# installs them in ThirdParty/Mac for the Pipecat plugin, with nlohmann/json's
# headers. They use Unreal's libcurl and OpenSSL, and download and build what
# else they need.
#
# Usage:
#   UE_ROOT=/path/to/UnrealEngine \
#   PIPECAT_CLIENT_CXX=/path/to/pipecat-client-cxx \
#     ./build-mac.sh
#

set -euo pipefail

: "${UE_ROOT:?Set UE_ROOT to your Unreal Engine directory}"
: "${PIPECAT_CLIENT_CXX:?Set PIPECAT_CLIENT_CXX to the pipecat-client-cxx source}"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/build/Mac"
PREFIX="$HERE/Mac"

# Unreal's libcurl and OpenSSL, which the Pipecat plugin links through Unreal.
# They're built for both architectures.
UE_THIRD_PARTY="$UE_ROOT/Engine/Source/ThirdParty"
CURL_DIR="$(ls -d "$UE_THIRD_PARTY"/libcurl/*/ | sort -V | tail -1)"
OPENSSL_DIR="$(ls -d "$UE_THIRD_PARTY"/OpenSSL/*/ | sort -V | tail -1)"

# The oldest macOS Unreal builds for. Libraries built for a newer one make the
# linker warn about each of their objects.
DEPLOYMENT_TARGET="$(plutil -extract DeploymentTarget raw -o - "$UE_ROOT/Engine/Config/Mac/Mac_SDK.json")"

rm -rf "$PREFIX"

echo "*** Building the Pipecat C++ client and its WebSocket transport ***"
# Unreal builds the editor for this Mac, and packages games for both
# architectures. The client downloads what it needs, instead of using e.g.
# Homebrew's, which are built for this Mac's macOS and against its OpenSSL.
cmake -S "$PIPECAT_CLIENT_CXX" -B "$BUILD/pipecat" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
    -DPIPECAT_BUILD_WEBSOCKET=ON \
    -DPIPECAT_BUILD_TESTS=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_nlohmann_json=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_LibDataChannel=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_SpeexDSP=ON \
    -DCURL_NO_CURL_CMAKE=ON \
    -DCURL_INCLUDE_DIR="$CURL_DIR/include" \
    -DCURL_LIBRARY="$CURL_DIR/lib/Mac/Release/libcurl.a" \
    -DOPENSSL_INCLUDE_DIR="$OPENSSL_DIR/include/Mac" \
    -DOPENSSL_SSL_LIBRARY="$OPENSSL_DIR/lib/Mac/libssl.a" \
    -DOPENSSL_CRYPTO_LIBRARY="$OPENSSL_DIR/lib/Mac/libcrypto.a"
cmake --build "$BUILD/pipecat"
cmake --install "$BUILD/pipecat" --prefix "$PREFIX"

# The Pipecat headers include nlohmann/json, which the client's build downloads.
if [[ ! -d "$PREFIX/include/nlohmann" ]]; then
    cp -R "$BUILD/pipecat/_deps/nlohmann_json-src/include/nlohmann" "$PREFIX/include/"
fi

echo "*** Installed in $PREFIX ***"
ls "$PREFIX/lib"
