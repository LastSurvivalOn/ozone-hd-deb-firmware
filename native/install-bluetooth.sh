#!/bin/bash
# Installs Bluetooth on top of the desktop (install-desktop.sh), for a USB adapter: btusb.ko (built
# here, bluetooth/build-btusb.sh), BlueZ, BlueALSA (sound to headphones and speakers, and from a
# phone to the TV) and ozone-bluetooth (the menu, the pairing agent, the panel icon).
# Usage: ./install-bluetooth.sh [box ip]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../config.sh"
IP=$(box_ip "${1:-}")

echo ">> btusb.ko (the stock kernel has no USB Bluetooth driver), built here"
[ -s "$HERE/out/bluetooth/btusb.ko" ] || "$HERE/bluetooth/build-btusb.sh"

echo ">> packages"
ssh_box 'set -e
    sudo apt-get install -y -qq --no-install-recommends bluez bluez-alsa-utils > /dev/null
    sudo apt-get clean
    dpkg -S /sbin/init | grep -q sysvinit-core || { echo "!! /sbin/init is not sysvinit any more" >&2; exit 1; }
    # BlueZ D-Bus policy: the menu and the agent run as the user
    sudo adduser -q "$(id -un)" bluetooth'

echo ">> files"
cat "$HERE/out/bluetooth/btusb.ko" | ssh_box 'sudo install -m 644 /dev/stdin /lib/modules/3.10.33/extra/btusb.ko'
tar -C "$HERE/rootfs" -czf - etc/init.d/ozone-bluetooth etc/init.d/ozone-hw etc/bluetooth/main.conf \
    etc/ozone/native-only usr/local/bin/ozone-bluetooth usr/local/bin/ozone-display \
    usr/local/lib/ozone/hotplug usr/local/lib/ozone/sound-config \
    usr/local/share/applications/ozone-bluetooth.desktop |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'
tar -C "$HERE/dotfiles" -czf - .config/bspwm/bspwmrc .config/polybar/config.ini | ssh_box 'tar -C ~ -xzf -'

echo ">> amplay (no AC-3 / DTS passthrough to Bluetooth), built on the box"
tar -C "$HERE/amplay" -czf - amplay.c osd.c osd.h ttx.c ttx.h Makefile |
    ssh_box 'set -e; d=$(mktemp -d); tar -C $d -xzf -; make -s -C $d; sudo make -s -C $d install; rm -r $d'

ssh_box 'set -e
    sudo update-rc.d ozone-bluetooth defaults > /dev/null
    /etc/ozone/is-native || exit 0
    # what ozone-hw does at boot: input devices plugged in later get the input group
    echo /usr/local/lib/ozone/hotplug | sudo tee /proc/sys/kernel/hotplug > /dev/null
    sudo /etc/init.d/bluetooth restart > /dev/null
    sudo /etc/init.d/ozone-bluetooth restart
    sync'
echo ">> done: Bluetooth in the apps menu and on the panel; restart the desktop (bspc quit) for"
echo "   the bluetooth group, the agent and the panel icon"
