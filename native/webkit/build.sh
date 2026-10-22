#!/bin/bash
# Cross-builds WebKitGTK for the box on the laptop: the same source package as the box's
# libwebkit2gtk-4.1-0 (Debian's 2.50.6-1~deb12u2, with its patches), its GTK 3 / libsoup 3 flavour
# only, with Debian's options and compiler (clang-16), in a Debian 12 container (Dockerfile here).
# The result is built for /usr/local/lib/ozone-webkit on the box and staged in
# ../cache/webkit/install (install-webkit.sh copies it over). Several hours the first time;
# afterwards only what a change touches is rebuilt.
#
# Differences from Debian's build, none of which changes the libraries' interface: no
# introspection (it can't be generated when cross-building; Debian's typelibs of the same version
# fit), no debug info, lld instead of ld.bfd, no MiniBrowser and no WebDriver.
#
# Usage: ./build.sh [configure|build|install|shell] [ninja args]
#   (default: patch and configure if needed, build, install into the staging directory)
# Progress: ./build.sh status, or tail -f ../cache/webkit/build.log (each line is [done/total]).
# Runs docker through the docker group (sg docker), so no new login is needed after joining it.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$HERE/../cache/webkit
VERSION=2.50.6
DEBIAN_REV=1~deb12u2
MIRROR=https://deb.debian.org/debian/pool/main/w/webkit2gtk
IMAGE=ozone-webkit-build
# A WebCore unit takes clang up to ~2.5 GB: 8 jobs filled the laptop's 15 GB, swapped, and froze
# the desktop. So fewer jobs, at the lowest CPU priority, and a memory limit on the container with
# no swap: past it the kernel kills one compiler in there, the build stops, and running it again
# goes on from where it was.
JOBS=${JOBS:-5}
MEMORY=${MEMORY:-$(( $(awk '/^MemTotal/ {print $2}' /proc/meminfo) / 1024 - 4096 ))m}   # all but 4 GB

mkdir -p "$WORK/src"
cd "$WORK/src"
for f in webkit2gtk_$VERSION-$DEBIAN_REV.dsc webkit2gtk_$VERSION-$DEBIAN_REV.debian.tar.xz webkit2gtk_$VERSION.orig.tar.xz; do
    [ -s "$f" ] || curl -fsSL -o "$f" "$MIRROR/$f"
done
# the tarballs must be the ones the .dsc names
for f in webkit2gtk_$VERSION-$DEBIAN_REV.debian.tar.xz webkit2gtk_$VERSION.orig.tar.xz; do
    want=$(sed -n '/^Checksums-Sha256:/,/^[A-Z]/p' webkit2gtk_$VERSION-$DEBIAN_REV.dsc | awk -v f="$f" '$3 == f {print $1}')
    [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$want" ] || { echo "$f: checksum mismatch" >&2; exit 1; }
done

docker_run() {
    local q
    q=$(printf '%q ' "$@")
    sg docker -c "docker --context default build -q -t $IMAGE '$HERE' > /dev/null &&
                  docker --context default run --rm -i --user $(id -u):$(id -g) -e JOBS=$JOBS \
                      --memory $MEMORY --memory-swap $MEMORY \
                      -v '$WORK:/work' -v '$HERE:/scripts:ro' -w /work $IMAGE $q"
}

running() { [ -n "$(sg docker -c "docker --context default ps -q --filter ancestor=$IMAGE")" ]; }
if [ "${1:-all}" != status ] && running; then
    echo "a build is running already (./build.sh status)" >&2
    exit 1
fi

case "${1:-all}" in
shell)
    docker_run bash
    ;;
status)
    # how far it is: the last [done/total] of the log, and whether the container still runs
    last=$(tr '\r' '\n' < "$WORK/build.log" 2>/dev/null | grep -o '^\[[0-9]*/[0-9]*\]' | tail -1)
    if running; then
        echo "running: ${last:-starting} (log: $WORK/build.log)"
    else
        echo "not running; last step ${last:-none}: $(tail -1 "$WORK/build.log" 2>/dev/null)"
    fi
    ;;
*)
    docker_run bash /scripts/build-inside.sh "$VERSION" "$DEBIAN_REV" "$@" 2>&1 | tee "$WORK/build.log"
    ;;
esac
