#!/bin/bash
# Installs DVB-T/T2 television for native Linux: dtvfe (built here with the Android NDK), the
# ozone-dtv init script that gives it an Android environment, and ozone-tv (scanner and player).
# Needs amplay and the Android files (install-desktop.sh, install-android-files.sh).
# Usage: ./install-tv.sh <box ip> [ndk dir]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
IP=${1:?specify the box IP}
KEY=$HOME/.ssh/id_ed25519_ozone
ssh_box() { ssh -i $KEY -o ConnectTimeout=10 <USER>@$IP "$@"; }

echo ">> dtvfe (Android/bionic, NDK)"
"$HERE/dtv/build.sh" ${2:+"$2"} > /dev/null

echo ">> files"
tar -C "$HERE/rootfs" -czf - etc/init.d/ozone-dtv usr/local/bin/ozone-tv |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'
ssh_box 'sudo install -D -m 755 /dev/stdin /usr/local/lib/ozone/dtvfe' < "$HERE/out/dtvfe"

echo ">> environment"
ssh_box 'set -e
    sudo update-rc.d ozone-dtv defaults > /dev/null
    sudo /etc/init.d/ozone-dtv start
    sync
    # a frontend check: the libraries find the demodulator and boot it (no antenna needed)
    echo quit | sudo /usr/local/lib/ozone/dtvfe 2> /dev/null'
echo ">> done: Super+T or the TV button on the panel, \"Scan channels\" first"
