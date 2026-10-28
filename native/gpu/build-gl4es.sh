#!/bin/bash
# Builds gl4es (github.com/ptitSeb/gl4es) on the box and installs it in /usr/local/lib/gl4es:
# OpenGL 2.1 turned into the Mali-450's OpenGL ES 2.0, for programs started with "ozone-gl --gl"
# (and the 3D programs Programs installs). Never system-wide: WebKit and GTK keep Mesa. cmake is
# installed for the build and removed after. The source is fetched into ../cache/gl4es.
# Usage: ./build-gl4es.sh [box ip]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../../config.sh"
IP=$(box_ip "${1:-}")
REV=c9895df34cd466c23bc60c2bd3db3d87e98fcbe7   # tag v1.1.6
CACHE=$HERE/../cache/gl4es
SRC=$CACHE/gl4es-$REV.tar.gz
mkdir -p "$CACHE"
[ -s "$SRC" ] || curl -fsSL -o "$SRC" "https://codeload.github.com/ptitSeb/gl4es/tar.gz/$REV"

echo ">> gl4es, built on the box (a few minutes)"
ssh_box 'set -e
    sudo apt-get install -y -qq --no-install-recommends cmake make gcc libx11-dev > /dev/null
    dpkg -S /sbin/init | grep -q sysvinit-core || { echo "!! /sbin/init is not sysvinit any more" >&2; exit 1; }'
ssh_box 'set -e; d=$(mktemp -d); trap "rm -rf $d" EXIT; tar -C $d -xzf -
    cd $d/gl4es-*; mkdir build; cd build
    # the S805: Cortex-A5 with NEON; GLES 2 is the default for X11 builds
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-mcpu=cortex-a5 -mfpu=neon-vfpv4 -mfloat-abi=hard" > /dev/null
    nice make -s -j4 > /dev/null
    sudo install -D -m 755 ../lib/libGL.so.1 /usr/local/lib/gl4es/libGL.so.1
    sudo ln -sf libGL.so.1 /usr/local/lib/gl4es/libGL.so
    sudo apt-get remove -y -qq cmake > /dev/null && sudo apt-get autoremove -y -qq > /dev/null
    sync; ls -la /usr/local/lib/gl4es' < "$SRC"
echo ">> done: ozone-gl --gl PROGRAM runs an OpenGL program on the GPU"
