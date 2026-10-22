#!/bin/bash
# Installs the Mali-450 GPU for native Linux X11 (see gpu/README.md):
# - the kernel modules (built here by gpu/build-modules.sh) and the ozone-gpu init script;
# - Mali's X11 userspace driver (r5p0) in /usr/local/lib/mali, behind our libEGL.so.1;
# - the Mali X driver (gpu/ddx), built on the box.
# The desktop picks the Mali X driver on its next start; programs use the GPU through ozone-gl,
# and mpv does by itself.
# Usage: ./install-gpu.sh [box ip]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../config.sh"
IP=$(box_ip "${1:-}")

LIBS_REV=31d5c21180f4d2f160332c7018ec91f8e43ca8bf   # github.com/mdrjr/c1_mali_libs: r5p0, vsync
CACHE=$HERE/cache/gpu
OUT=$HERE/out/gpu

echo ">> kernel modules"
[ -f "$OUT/fbump.ko" ] || "$HERE/gpu/build-modules.sh" > /dev/null
tar -C "$OUT" --owner=0 --group=0 --mode=u=rwX,go=rX -czf - . | ssh_box 'set -e
    sudo mkdir -p /lib/modules/3.10.33/extra/gpu
    sudo tar -C /lib/modules/3.10.33/extra/gpu --no-same-owner -xzf -'

echo ">> Mali userspace driver (X11, r5p0)"
mkdir -p "$CACHE"
LIBS=$CACHE/c1_mali_libs-$LIBS_REV.tar.gz
[ -s "$LIBS" ] || curl -fsSL -o "$LIBS" https://codeload.github.com/mdrjr/c1_mali_libs/tar.gz/$LIBS_REV
D=$(mktemp -d)
tar -C "$D" -xzf "$LIBS" --strip-components=2 --wildcards '*/x11/mali_libs/libMali.so' \
    '*/x11/mali_libs/libUMP.so' '*/x11/mali_headers'
cp -r "$HERE/gpu/ddx" "$HERE/gpu/egl-fix" "$D/"
tar -C "$D" -czf - . | ssh_box 'set -e; d=$(mktemp -d); tar -C $d -xzf -
    L=/usr/local/lib/mali I=/usr/local/include/mali
    sudo mkdir -p $L $I
    sudo install -m 755 $d/mali_libs/libMali.so $d/mali_libs/libUMP.so $L/
    sudo cp -r $d/mali_headers/. $I/
    # build tools and headers for the X driver
    sudo apt-get install -y -qq --no-install-recommends gcc make xserver-xorg-dev libdrm-dev libx11-dev > /dev/null
    # our libEGL.so.1 (and libGLESv2.so.2) in front of libMali.so; GLES 1 straight from libMali.so
    gcc -O2 -Wall -shared -fPIC -Wl,-soname,libEGL.so.1 -Wl,-Bsymbolic -I $I \
        $d/egl-fix/egl-fix.c $d/egl-fix/gles-fix.c -o $d/libEGL.so.1 -L $L -Wl,--no-as-needed -lMali -Wl,--as-needed -lUMP -lX11 -lXext -ldl -lpthread
    sudo install -m 755 $d/libEGL.so.1 $L/
    cd $L
    for l in libEGL.so libGLESv2.so.2 libGLESv2.so; do sudo ln -sf libEGL.so.1 $l; done
    for l in libGLESv1_CM.so.1 libGLESv1_CM.so; do sudo ln -sf libMali.so $l; done
    # for programs of your own: gcc prog.c $(pkg-config --cflags --libs mali-gles), run with ozone-gl
    sudo mkdir -p /usr/local/lib/pkgconfig
    printf "%s\n" "libdir=$L" "includedir=$I" "" "Name: mali-gles" \
        "Description: OpenGL ES 2.0 and EGL (X11) on the Mali-450; run programs with ozone-gl" \
        "Version: r5p0" "Cflags: -I\${includedir}" \
        "Libs: -L\${libdir} -Wl,-rpath-link,\${libdir} -lEGL -lGLESv2 -lMali" | sudo tee /usr/local/lib/pkgconfig/mali-gles.pc > /dev/null
    echo ">> Mali X driver, built on the box"
    make -s -C $d/ddx MALI_INC=$I MALI_LIB=$L
    sudo make -s -C $d/ddx install
    rm -r $d'
rm -r "$D"

echo ">> system files"
tar -C "$HERE/rootfs" -czf - etc/init.d/ozone-gpu usr/local/lib/ozone/gen-xorg-screen \
    usr/local/lib/ozone/desktop usr/local/bin/ozone-gl usr/local/bin/mpv |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'

ssh_box 'set -e
    sudo update-rc.d ozone-gpu defaults > /dev/null
    # sysvinit stays (apt can trade it for systemd through dependencies)
    dpkg -S /sbin/init | grep -q sysvinit-core || { echo "!! /sbin/init is not sysvinit any more" >&2; exit 1; }
    # Android'"'"'s mali.ko (from extra/) can neither serve X11 nor be unloaded
    if grep -q "^mali " /proc/modules && ! grep -q "^ump " /proc/modules; then
        echo ">> the stock mali.ko is loaded: reboot to switch to the GPU"
    else
        sudo /etc/init.d/ozone-gpu start
        echo ">> GPU ready: restart X (or reboot) for the desktop to use it"
    fi
    sync'
