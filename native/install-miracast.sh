#!/bin/bash
# Installs screen mirroring (Miracast) on top of install-network.sh: wpa_supplicant-p2p (Debian's,
# patched for the 8189ES, built on the box: miracast/build-wpa.sh), ozone-miracast and amplay
# with --rate. "Screen mirroring" in the apps menu and the Network menu then waits for a phone.
# Usage: ./install-miracast.sh [box ip]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../config.sh"
IP=$(box_ip "${1:-}")

"$HERE/miracast/build-wpa.sh" "$IP"

echo ">> files"
tar -C "$HERE/rootfs" -czf - usr/local/bin/ozone-miracast usr/local/lib/ozone/miracast-dhcp \
    usr/local/bin/ozone-network usr/local/share/applications/ozone-miracast.desktop |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'

echo ">> amplay (--rate, for the frame rate of a mirrored screen), built on the box"
tar -C "$HERE/amplay" -czf - amplay.c osd.c osd.h ttx.c ttx.h Makefile |
    ssh_box 'set -e; d=$(mktemp -d); tar -C $d -xzf -; make -s -C $d; sudo make -s -C $d install; rm -r $d; sync'
echo ">> done: Screen mirroring in the apps menu (or Network → Show a phone's screen)"
