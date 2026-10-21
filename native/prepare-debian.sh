#!/bin/bash
# Prepares the already installed Debian (/data/linux/debian.img) to boot on its own:
# sysvinit, network, time, kernel modules from Android, hardware setup.
# Chroot mode under Android keeps working as before.
# Usage: ./prepare-debian.sh <box ip>
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
IP=${1:?specify the box IP}
KEY=$HOME/.ssh/id_ed25519_ozone
ROOT=/data/linux/root
KVER=3.10.33
STOCK=${STOCK:-$HOME/ozone_backup/backup/boot.img}

ssh_box() { ssh -i $KEY -o ConnectTimeout=10 <USER>@$IP "$@"; }
adb_root() { adb -s $IP:5555 shell "su -c '$*'" | tr -d '\r'; }

echo ">> kernel modules"
# mali.ko is in the stock ramdisk, the rest in /system/lib
W=$(mktemp -d)
python3 -I "$HERE/tools/bootimg.py" unpack "$STOCK" "$W"
zcat "$W/ramdisk" | (cd "$W" && cpio -id --quiet --no-absolute-filenames boot/mali.ko)
adb -s $IP:5555 push "$W/boot/mali.ko" /data/local/tmp/mali.ko >/dev/null
rm -r "$W"
adb_root "mkdir -p $ROOT/lib/modules/$KVER/extra && \
          cp /system/lib/8189es.ko /system/lib/aml_thermal.ko /data/local/tmp/mali.ko $ROOT/lib/modules/$KVER/extra/ && \
          chmod 644 $ROOT/lib/modules/$KVER/extra/*.ko && rm /data/local/tmp/mali.ko"

echo ">> config files"
# rootfs/ has wrappers for these two stock scripts; the stock ones move aside first, once
ssh_box 'sudo mkdir -p /usr/local/lib/ozone && for s in halt reboot; do
    sudo dpkg-divert --local --rename --divert /usr/local/lib/ozone/$s.distrib --add /etc/init.d/$s >/dev/null
done'
tar -C "$HERE/rootfs" -czf - . | ssh_box 'sudo tar -C / --no-same-owner -xzf - && sudo chown -R root:root /etc/init.d/ozone-hw /etc/ozone /etc/network/interfaces /etc/fstab'
scp -q -i $KEY "$HERE/../hybrid/box/rc" <USER>@$IP:/tmp/ozone-rc
ssh_box 'sudo install -m 755 /tmp/ozone-rc /etc/ozone/rc && rm /tmp/ozone-rc && sudo chmod 755 /etc/init.d/ozone-hw /etc/ozone/is-native'

echo ">> packages"
ssh_box 'sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    sysvinit-core ifupdown isc-dhcp-client kmod fbset chrony fake-hwclock'

echo ">> settings"
ssh_box 'set -e
    # Under Android, Android keeps the time; chrony is only for pure Linux
    sudo /etc/init.d/chrony stop >/dev/null 2>&1 || true
    # chronyd drops supplementary groups, so aid_inet must be its primary group (ANDROID_PARANOID_NETWORK)
    sudo usermod -g aid_inet _chrony
    # -F 1 (seccomp filter) keeps chronyd from starting on a kernel without seccomp
    sudo sed -i "s/^DAEMON_OPTS=.*/DAEMON_OPTS=\"-F 0\"/" /etc/default/chrony
    sudo depmod -a '$KVER'
    grep -qx aml_thermal /etc/modules || echo aml_thermal | sudo tee -a /etc/modules >/dev/null
    # remove first: insserv keeps the runlevels of existing links over changed LSB headers
    sudo update-rc.d ozone-hw remove && sudo update-rc.d ozone-hw defaults
    ls -l /usr/sbin/init /lib/modules/'$KVER'/extra/'
echo ">> done"
