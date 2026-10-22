#!/bin/bash
# Replaces the boot picture: u-boot shows it at power-on (the logo partition, in both boot modes),
# and native Linux takes it over on fb0 and makes it pulse until X starts (/usr/local/lib/ozone/splash).
# Usage: ./install-logo.sh [box ip] [1920x1080 png]    (default: logo/bootup.png)
#        ./install-logo.sh <box ip> --stock            (puts the stock partition back, no splash)
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../config.sh"
IP=$(box_ip "${1:-}")
PNG=${2:-$HERE/logo/bootup.png}
STOCK=$HOME/ozone_backup/backup/logo.img
SPLASH=/usr/share/ozone/splash.rgb565

[ -f "$STOCK" ] || { echo "missing $STOCK (backup of the logo partition)"; exit 1; }
mkdir -p "$HERE/out"
IMG=$HERE/out/logo.img
if [ "$PNG" = --stock ]; then
    cp "$STOCK" "$IMG"
else
    python3 -I "$HERE/tools/logoimg.py" pack "$STOCK" "$PNG" "$IMG"
    python3 -I "$HERE/tools/logoimg.py" fb "$PNG" "$HERE/out/splash.rgb565"
fi
SIZE=$(stat -c %s "$IMG")
SUM=$(md5sum < "$IMG" | cut -d' ' -f1)

ssh_box '/etc/ozone/is-native' || { echo "the box must be running native Linux (/dev/logo)"; exit 1; }
echo ">> writing $SIZE bytes to /dev/logo"
ssh_box "sudo dd of=/dev/logo bs=64k conv=fsync 2>/dev/null && sync" < "$IMG"
[ "$(ssh_box "sudo head -c $SIZE /dev/logo | md5sum" | cut -d' ' -f1)" = "$SUM" ] ||
    { echo "!! read-back mismatch: write the stock image back with --stock"; exit 1; }

echo ">> boot splash $SPLASH"
if [ "$PNG" = --stock ]; then
    ssh_box "sudo rm -f $SPLASH"
else
    ssh_box "sudo mkdir -p ${SPLASH%/*} && sudo tee $SPLASH > /dev/null && sync" < "$HERE/out/splash.rgb565"
fi
echo ">> done, visible on the next power-on"
