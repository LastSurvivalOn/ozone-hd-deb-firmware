#!/bin/bash
# Builds wpa_supplicant-p2p on the box: Debian's wpa_supplicant (the same version and patches)
# with patches/wpa-rtw-p2p.patch, for WiFi Direct with the 8189ES (ozone-miracast). The system's
# wpa_supplicant (wlan0) stays as it is. The source is fetched into ../cache/wpa.
# Usage: ./build-wpa.sh [box ip]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../../config.sh"
IP=$(box_ip "${1:-}")
VERSION=2.10
DEBIAN=2.10-12+deb12u3   # as `dpkg -l wpasupplicant` on the box
POOL=https://deb.debian.org/debian/pool/main/w/wpa
CACHE=$HERE/../cache/wpa
mkdir -p "$CACHE"
for f in wpa_$VERSION.orig.tar.xz wpa_$DEBIAN.debian.tar.xz; do
    [ -s "$CACHE/$f" ] || curl -fsSL -o "$CACHE/$f" "$POOL/$f"
done

D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
tar -C "$D" -xJf "$CACHE/wpa_$VERSION.orig.tar.xz"
S=$D/wpa-$VERSION
tar -C "$S" -xJf "$CACHE/wpa_$DEBIAN.debian.tar.xz"
for p in $(grep -v '^#' "$S/debian/patches/series"); do
    patch -s -d "$S" -p1 < "$S/debian/patches/$p"
done
patch -s -d "$S" -p1 < "$HERE/patches/wpa-rtw-p2p.patch"
# Debian's configuration, without what needs more libraries than the box has for building
sed -e 's/^CONFIG_PCSC=y/#&/' -e 's/^CONFIG_CTRL_IFACE_DBUS/#&/' -e 's/^CONFIG_READLINE=y/#&/' \
    "$S/debian/config/wpasupplicant/linux" > "$S/wpa_supplicant/.config"

echo ">> wpa_supplicant-p2p, built on the box"
tar -C "$D" -czf - "wpa-$VERSION/src" "wpa-$VERSION/wpa_supplicant" |
    ssh_box 'set -e; d=$(mktemp -d); tar -C $d -xzf -
        sudo apt-get install -y -qq --no-install-recommends gcc make pkg-config libssl-dev libnl-3-dev \
            libnl-genl-3-dev libnl-route-3-dev > /dev/null
        dpkg -S /sbin/init | grep -q sysvinit-core || { echo "!! /sbin/init is not sysvinit any more" >&2; exit 1; }
        make -s -C $d/wpa-*/wpa_supplicant -j4 wpa_supplicant > /dev/null
        sudo install -s -m 755 $d/wpa-*/wpa_supplicant/wpa_supplicant /usr/local/sbin/wpa_supplicant-p2p
        rm -r $d; sync
        /usr/local/sbin/wpa_supplicant-p2p -v | head -1'
