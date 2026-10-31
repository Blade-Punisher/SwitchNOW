#!/usr/bin/env bash
set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="/mingw64/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"

cd "$(dirname "$0")/.."

switchnow_version="${SWITCHNOW_VERSION:-1.3.0}"

network_debug="${SWITCHNOW_NETWORK_DEBUG:-OFF}"
cmake -B build/switch -G Ninja -DCMAKE_BUILD_TYPE=Release -DPROJECT_VERSION_STRING="$switchnow_version" -DSWITCHNOW_NETWORK_DEBUG="$network_debug"
cmake --build build/switch --target cjson mbedtls srtp2 usrsctp -j"${OPENNOW_BUILD_JOBS:-$(nproc)}"
cmake --build build/switch --target SwitchNOW.nro -j"${OPENNOW_BUILD_JOBS:-$(nproc)}"

printf '\nBuilt: %s\n' "$(pwd)/build/switch/SwitchNOW.nro"
