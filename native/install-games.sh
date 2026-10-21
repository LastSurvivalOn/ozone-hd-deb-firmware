#!/bin/bash
# Installs games on the GPU (needs install-gpu.sh): RetroArch with the NES, Game Boy (Color)
# and GBA cores, and glmark2 to measure the GPU. Games go in ~/Games (or on a USB drive).
# Usage: ./install-games.sh <box ip>
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
IP=${1:?specify the box IP}
KEY=$HOME/.ssh/id_ed25519_ozone
# the same host key on Ethernet (.200) and WiFi
ssh_box() { ssh -i $KEY -o ConnectTimeout=10 -o HostKeyAlias=<BOX_IP> <USER>@$IP "$@"; }

echo ">> files"
tar -C "$HERE/rootfs" -czf - usr/local/bin/retroarch etc/ozone/retroarch.cfg etc/ozone/retroarch-user.cfg |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'

echo ">> RetroArch"
ssh_box 'set -e
    # retroarch-assets is the menu artwork and pulls in some 300 MB of fonts, more than the
    # system image has; the built-in RGUI menu needs none of it: an empty package instead
    if ! dpkg -s retroarch-assets > /dev/null 2>&1; then
        d=$(mktemp -d); mkdir $d/DEBIAN
        printf "%s\n" "Package: retroarch-assets" "Version: 1.7.6+git20221024+dfsg-3+ozone1" \
            "Architecture: all" "Maintainer: ozone <root@localhost>" \
            "Description: placeholder for the RetroArch menu assets" \
            " RetroArch runs with its built-in RGUI menu, which needs no assets." > $d/DEBIAN/control
        dpkg-deb -b $d $d/assets.deb > /dev/null
        sudo dpkg -i $d/assets.deb > /dev/null
        rm -r $d
    fi
    sudo apt-mark hold retroarch-assets > /dev/null
    sudo apt-get install -y -qq --no-install-recommends retroarch libretro-nestopia libretro-gambatte \
        libretro-mgba glmark2-es2-x11 > /dev/null
    sudo apt-get clean
    # sysvinit stays (apt can trade it for systemd through dependencies)
    dpkg -S /sbin/init | grep -q sysvinit-core || { echo "!! /sbin/init is not sysvinit any more" >&2; exit 1; }
    mkdir -p ~/Games ~/.config/retroarch
    [ -e ~/.config/retroarch/retroarch.cfg ] || cp /etc/ozone/retroarch-user.cfg ~/.config/retroarch/retroarch.cfg
    sync
    df -h / | tail -1'
echo ">> done: RetroArch in the menu (Super+D), games in ~/Games"
