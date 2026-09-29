#!/usr/bin/env bash
#
# Copyright (c) 2026, Daily
#
# Builds the Pipecat C++ client and its Daily transport with Unreal Engine's
# toolchain, and installs them in ThirdParty/Linux for the Pipecat plugin, with
# nlohmann/json's headers and the Daily Core shared library.
#
# Usage:
#   UE_ROOT=/path/to/UnrealEngine \
#   PIPECAT_CLIENT_CXX=/path/to/pipecat-client-cxx \
#   PIPECAT_CLIENT_CXX_DAILY=/path/to/pipecat-client-cxx-daily \
#   DAILY_CORE_SDK=/path/to/daily-core-sdk \
#     ./build-linux.sh
#

set -euo pipefail

: "${UE_ROOT:?Set UE_ROOT to your Unreal Engine directory}"
: "${PIPECAT_CLIENT_CXX:?Set PIPECAT_CLIENT_CXX to the pipecat-client-cxx source}"
: "${PIPECAT_CLIENT_CXX_DAILY:?Set PIPECAT_CLIENT_CXX_DAILY to the pipecat-client-cxx-daily source}"
: "${DAILY_CORE_SDK:?Set DAILY_CORE_SDK to the Daily Core C++ SDK (0.23.0 or newer)}"
export UE_ROOT

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/build/Linux"
PREFIX="$HERE/Linux"
TOOLCHAIN="$HERE/unreal-linux.cmake"

# Unreal's libcurl, which the Pipecat plugin links through Unreal.
CURL_DIR="$(ls -d "$UE_ROOT"/Engine/Source/ThirdParty/libcurl/*/ | sort -V | tail -1)"
CURL_INCLUDE="$CURL_DIR/include"
CURL_LIBRARY="$CURL_DIR/lib/Unix/x86_64-unknown-linux-gnu/Release/libcurl.a"

rm -rf "$PREFIX"

echo "*** Building the Pipecat C++ client ***"
cmake -S "$PIPECAT_CLIENT_CXX" -B "$BUILD/pipecat" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCURL_INCLUDE_DIR="$CURL_INCLUDE" \
    -DCURL_LIBRARY="$CURL_LIBRARY"
cmake --build "$BUILD/pipecat"
cmake --install "$BUILD/pipecat" --prefix "$PREFIX"

# The Pipecat headers include nlohmann/json. Unreal's sysroot doesn't have it,
# so the client's build downloads it.
if [[ ! -d "$PREFIX/include/nlohmann" ]]; then
    cp -R "$BUILD/pipecat/_deps/nlohmann_json-src/include/nlohmann" "$PREFIX/include/"
fi

echo "*** Building the Daily transport ***"
cmake -S "$PIPECAT_CLIENT_CXX_DAILY" -B "$BUILD/pipecat_daily" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DDailyCore_ROOT="$DAILY_CORE_SDK"
cmake --build "$BUILD/pipecat_daily"
cmake --install "$BUILD/pipecat_daily" --prefix "$PREFIX"

# Daily Core is built with GCC, so it's linked as its shared library, which
# only exports its C API.
cp "$DAILY_CORE_SDK/lib/libdaily_core.so" "$PREFIX/lib/"

echo "*** Installed in $PREFIX ***"
ls "$PREFIX/lib"
