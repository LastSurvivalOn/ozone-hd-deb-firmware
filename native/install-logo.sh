#!/bin/bash
# Replaces the picture u-boot shows at power-on (the "OzoneHD" logo) in the logo partition.
# It stays on screen until Android's boot animation or X starts, in both boot modes.
# Usage: ./install-logo.sh <box ip> [1280x720 png]    (default: logo/bootup.png)
#        ./install-logo.sh <box ip> --stock           (puts the stock partition back)
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
IP=${1:?specify the box IP}
PNG=${2:-$HERE/logo/bootup.png}
STOCK=$HOME/ozone_backup/backup/logo.img
KEY=$HOME/.ssh/id_ed25519_ozone
ssh_box() { ssh -i $KEY -o ConnectTimeout=10 <USER>@$IP "$@"; }

[ -f "$STOCK" ] || { echo "missing $STOCK (backup of the logo partition)"; exit 1; }
mkdir -p "$HERE/out"
IMG=$HERE/out/logo.img
if [ "$PNG" = --stock ]; then
    cp "$STOCK" "$IMG"
else
    python3 -I "$HERE/tools/logoimg.py" pack "$STOCK" "$PNG" "$IMG"
fi
SIZE=$(stat -c %s "$IMG")
SUM=$(md5sum < "$IMG" | cut -d' ' -f1)

ssh_box '/etc/ozone/is-native' || { echo "the box must be running native Linux (/dev/logo)"; exit 1; }
echo ">> writing $SIZE bytes to /dev/logo"
ssh_box "sudo dd of=/dev/logo bs=64k conv=fsync 2>/dev/null && sync" < "$IMG"
[ "$(ssh_box "sudo head -c $SIZE /dev/logo | md5sum" | cut -d' ' -f1)" = "$SUM" ] ||
    { echo "!! read-back mismatch: write the stock image back with --stock"; exit 1; }
echo ">> done, visible on the next power-on"
