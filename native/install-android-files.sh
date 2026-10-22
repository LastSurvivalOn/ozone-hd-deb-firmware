#!/bin/bash
# Copies the Android files native Debian still runs (android-files.txt: Android's linker, the TV
# frontend libraries, remotecfg for the IR remote) out of the backup of the stock system partition
# into /usr/local/lib/ozone/android on the box, and installs ozone-android, which mounts them on
# /system at boot. The box's own system partition is no longer read, so Android can go.
# Needs debugfs (e2fsprogs) here; no root.
# Usage: ./install-android-files.sh <box ip> [system.img]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
IP=${1:?specify the box IP}
IMG=${2:-$HOME/ozone_backup/backup/system.img}
KEY=$HOME/.ssh/id_ed25519_ozone
ssh_box() { ssh -i $KEY -o ConnectTimeout=10 <USER>@$IP "$@"; }

[ -s "$IMG" ] || { echo "no $IMG: the stock system partition's backup (see ../CLAUDE.md)" >&2; exit 1; }

echo ">> the Android files, out of $IMG"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
grep -v '^#' "$HERE/android-files.txt" | grep . | while read -r f; do
    rel=${f#/system/}
    mkdir -p "$W/android/$(dirname "$rel")"
    debugfs -R "dump -p /$rel $W/android/$rel" "$IMG" > /dev/null 2>&1 || true
    [ -s "$W/android/$rel" ] || { echo "not in $IMG: $f" >&2; exit 1; }
done
du -sh "$W/android" | cut -f1

echo ">> to the box"
tar -C "$W" --owner=0 --group=0 -czf - android |
    ssh_box 'sudo tar -C /usr/local/lib/ozone --no-same-owner -xzf -'
tar -C "$HERE/rootfs" -czf - etc/init.d/ozone-android etc/init.d/ozone-dtv etc/init.d/ozone-input |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'
ssh_box 'set -e
    sudo update-rc.d ozone-android defaults > /dev/null
    # the TV service now starts after it
    [ -e /etc/init.d/ozone-dtv ] && sudo update-rc.d ozone-dtv defaults > /dev/null
    # /system from the copy from now on: the NAND partition goes if nothing holds it
    if grep -q "^/dev/system /system " /proc/mounts && ! sudo umount /system 2> /dev/null; then
        echo "/system is busy (the TV running?): the copy takes over at the next boot"
    fi
    sudo /etc/init.d/ozone-android start
    sync
    grep " /system " /proc/mounts'
