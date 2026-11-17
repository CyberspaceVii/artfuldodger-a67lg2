#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
NDK_DIR=${NDK_DIR:-/opt/android-ndk}
CXX="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++"
CC="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
mkdir -p zygisk
"$CXX" --target=aarch64-linux-android34 -std=c++17 -Wall -Wextra -Werror -shared -fPIC -O2 -fvisibility=hidden -static-libstdc++ -Isource -o zygisk/arm64-v8a.so source/capmod.cpp -llog -ldl
"$CC" --target=aarch64-linux-android34 -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -O2 -fPIE -pie -o artfuldodgerd source/artfuldodgerd.c -llog
