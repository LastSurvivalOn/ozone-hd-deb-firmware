#!/bin/sh
# Builds dtvfe, an Android (bionic) program, with the NDK. The box's Android 4.4 linker only knows
# the SysV symbol hash and plain relocations, so no GNU hash and no packed/RELR relocations.
# Usage: ./build.sh [ndk dir]    (default: the newest in ~/Android/Sdk/ndk)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
NDK=${1:-$(ls -d "$HOME"/Android/Sdk/ndk/* 2>/dev/null | sort -V | tail -1)}
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang
[ -x "$CC" ] || { echo "no NDK clang at $CC"; exit 1; }
mkdir -p "$HERE/../out"
"$CC" -O2 -Wall -fPIE -pie -Wl,--hash-style=sysv -Wl,-z,nopack-relative-relocs \
    -o "$HERE/../out/dtvfe" "$HERE/dtvfe.c" -ldl
echo "$HERE/../out/dtvfe"
