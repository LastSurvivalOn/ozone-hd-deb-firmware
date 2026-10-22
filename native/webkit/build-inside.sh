#!/bin/bash
# The part of build.sh that runs in the container, in /work (../cache/webkit on the laptop).
# Usage: build-inside.sh VERSION DEBIAN_REV [configure|build|install|all] [ninja args]
set -euo pipefail
VERSION=$1 DEBIAN_REV=$2
STEP=${3:-all}
shift 3 || shift $#
SRC=/work/webkitgtk-$VERSION
BUILD=/work/build
# Where it lives on the box, next to Debian's WebKit: the library starts its helper processes
# (WebKitWebProcess...) and loads its injected bundle from the paths built in, so they are ours
PREFIX=/usr/local/lib/ozone-webkit

if [ ! -e "$SRC/.ozone-patched" ]; then
    echo ">> source, with Debian's patches"
    rm -rf "$SRC"
    tar -C /work -xJf /work/src/webkit2gtk_$VERSION.orig.tar.xz
    tar -C "$SRC" -xJf /work/src/webkit2gtk_$VERSION-$DEBIAN_REV.debian.tar.xz
    grep -v '^#' "$SRC/debian/patches/series" | grep . | while read -r p; do
        patch -d "$SRC" -p1 -s < "$SRC/debian/patches/$p"
    done
    touch "$SRC/.ozone-patched"
fi
# our own changes (patches/), in order; each once, so that a new one only rebuilds what it touches
for p in /scripts/patches/*.patch; do
    [ -e "$p" ] || continue
    n=$(basename "$p")
    grep -qx "$n" "$SRC/.ozone-patched" && continue
    echo ">> $n"
    patch -d "$SRC" -p1 -s --forward < "$p"
    echo "$n" >> "$SRC/.ozone-patched"
done

# Debian's armhf build flags (debian/rules: hardening=+all, -lto), without the debug info, for
# Debian's armhf baseline (ARMv7, VFPv3-D16, Thumb-2)
ARCH="--target=arm-linux-gnueabihf -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -mthumb"
eval "$(DEB_HOST_ARCH=armhf DEB_BUILD_MAINT_OPTIONS='hardening=+all optimize=-lto' dpkg-architecture -aarmhf -c dpkg-buildflags --export=sh)"
CFLAGS="${CFLAGS//-g /} $ARCH" CXXFLAGS="${CXXFLAGS//-g /} $ARCH"
CPPFLAGS="$CPPFLAGS -DNDEBUG -DG_DISABLE_CAST_CHECKS"
LDFLAGS="$LDFLAGS -fuse-ld=lld $ARCH"

if [ "$STEP" != build ] && { [ "$STEP" = configure ] || [ ! -e "$BUILD/build.ninja" ]; }; then
    echo ">> configure"
    mkdir -p "$BUILD"
    cat > "$BUILD/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)
set(CMAKE_LIBRARY_ARCHITECTURE arm-linux-gnueabihf)
set(CMAKE_C_COMPILER clang-16)
set(CMAKE_CXX_COMPILER clang++-16)
set(CMAKE_C_COMPILER_TARGET arm-linux-gnueabihf)
set(CMAKE_CXX_COMPILER_TARGET arm-linux-gnueabihf)
set(PKG_CONFIG_EXECUTABLE /usr/bin/arm-linux-gnueabihf-pkg-config)
EOF
    cd "$BUILD"
    CFLAGS="$CPPFLAGS $CFLAGS" CXXFLAGS="$CPPFLAGS $CXXFLAGS" LDFLAGS="$LDFLAGS" \
    cmake "$SRC" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$BUILD/toolchain.cmake" \
        -DPORT=GTK -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_LIBEXECDIR=libexec -DCMAKE_INSTALL_RPATH=$PREFIX/lib \
        -DCMAKE_INSTALL_LOCALEDIR=/usr/share/locale \
        -DEXEC_INSTALL_DIR=$PREFIX/bin -DLIB_INSTALL_DIR=$PREFIX/lib \
        -DLIBEXEC_INSTALL_DIR=$PREFIX/libexec/webkit2gtk-4.1 \
        -DCMAKE_C_FLAGS_RELEASE= -DCMAKE_CXX_FLAGS_RELEASE= -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
        -DUSE_LIBBACKTRACE=OFF -DUSE_JPEGXL=OFF -DUSE_SYSPROF_CAPTURE=OFF -DFORCE_32BIT=ON \
        -DUSE_SOUP2=OFF -DUSE_GTK4=OFF -DENABLE_BUBBLEWRAP_SANDBOX=ON \
        -DENABLE_DOCUMENTATION=OFF -DENABLE_INTROSPECTION=OFF \
        -DENABLE_MINIBROWSER=OFF -DENABLE_WEBDRIVER=OFF \
        -DBWRAP_EXECUTABLE=/usr/bin/bwrap -DDBUS_PROXY_EXECUTABLE=/usr/bin/xdg-dbus-proxy
fi

if [ "$STEP" = build ] || [ "$STEP" = all ]; then
    echo ">> build ($JOBS jobs)"
    nice -n 19 ninja -C "$BUILD" -j "$JOBS" "$@"
fi

if [ "$STEP" = install ] || [ "$STEP" = all ]; then
    # staged in /work/install: what the box needs of it is picked by install-webkit.sh
    echo ">> install into /work/install$PREFIX"
    rm -rf /work/install
    DESTDIR=/work/install ninja -C "$BUILD" install > /dev/null
    # no symbol tables, as in Debian's packages (it keeps them in -dbgsym ones): 101 MB to ~63
    find /work/install$PREFIX/lib -name '*.so*' -type f -exec arm-linux-gnueabihf-strip --strip-unneeded {} +
    find /work/install$PREFIX/libexec -type f -exec arm-linux-gnueabihf-strip --strip-unneeded {} +
fi
