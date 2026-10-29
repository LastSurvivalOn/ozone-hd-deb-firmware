#!/bin/bash
# Removes Android from the box for good, leaving only native Linux (which must be running):
#  - the recovery partition (Android's boot image) becomes a copy of the boot partition, so u-boot
#    still has a Linux to fall back on when it can't load the boot partition;
#  - the system (1 GB) and cache (512 MB) partitions become ext4 for Linux: /etc/init.d/ozone-storage
#    puts ~/Downloads and apt's package downloads on system, ~/.cache on cache;
#  - on the data partition, everything of Android's (apps, their data, its internal storage) goes;
#    linux/ (debian.img, the TV recordings, the logs, the backups) stays;
#  - /etc/ozone/no-android: the power menu has no "Android" then, and reboot-android refuses.
# --undo puts Android back from the partition backups in ~/ozone_backup/backup, which must be there
# and match their md5.txt before anything is removed.
# Usage: ./remove-android.sh [box ip]           then restart the box
#        ./remove-android.sh --undo [box ip]    then restart the box
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../config.sh"
UNDO=0
if [ "${1:-}" = --undo ]; then
    UNDO=1
    shift
fi
IP=$(box_ip "${1:-}")
B=$HOME/ozone_backup/backup
MKFS='mkfs.ext4 -F -q -m 0 -E nodiscard -O ^metadata_csum,^metadata_csum_seed,^orphan_file'

die() { echo "!! $*" >&2; exit 1; }

echo ">> the backups in $B"
for f in boot.img system.img cache.img.gz android-data.tar.gz md5.txt; do
    [ -f "$B/$f" ] || die "missing $B/$f"
done
(cd "$B" && grep -E '  (boot\.img|system\.img|cache\.img\.gz|android-data\.tar\.gz)$' md5.txt | md5sum --quiet -c -) ||
    die "the backups don't match md5.txt"
ssh_box /etc/ozone/is-native || die "the box must be running native Linux"

# a partition's first SIZE bytes against a file's
same() {   # device file
    local size sum
    size=$(stat -c %s "$2")
    sum=$(md5sum < "$2" | cut -d' ' -f1)
    [ "$(ssh_box "sudo head -c $size $1 | md5sum" | cut -d' ' -f1)" = "$sum" ]
}

if [ $UNDO = 0 ]; then
    ssh_box "grep -qE '^/dev/(system|cache) ' /proc/mounts" && die "system or cache is mounted on the box"
    ssh_box '[ -e /etc/ozone/no-android ]' && die "Android is removed already (--undo puts it back)"

    echo ">> /etc/init.d/ozone-storage"
    ssh_box 'sudo install -m 755 /dev/stdin /etc/init.d/ozone-storage' < "$HERE/rootfs/etc/init.d/ozone-storage"
    ssh_box 'sudo update-rc.d ozone-storage defaults > /dev/null'

    ssh_box 'sudo sh -s' << 'EOF'
set -e
echo '>> recovery: a copy of the boot partition'
dd if=/dev/boot of=/dev/recovery bs=64k conv=fsync 2> /dev/null
[ "$(md5sum < /dev/boot)" = "$(md5sum < /dev/recovery)" ] || { echo '!! recovery differs from boot'; exit 1; }
sync
EOF

    echo ">> system and cache: ext4 for Linux"
    ssh_box "sudo $MKFS -L storage /dev/system && sudo $MKFS -L cache /dev/cache && sync"

    echo ">> Android's files on the data partition"
    ssh_box 'sudo sh -s' << 'EOF'
set -e
mountpoint -q /mnt/nand && [ -f /mnt/nand/linux/debian.img ] || { echo '!! /mnt/nand is not the data partition'; exit 1; }
before=$(df -m /mnt/nand | awk 'NR == 2 {print $4}')
find /mnt/nand -mindepth 1 -maxdepth 1 ! -name linux ! -name lost+found -exec rm -rf {} +
# what Debian under Android (hybrid/) used
rm -f /mnt/nand/linux/ozlinux.sh /mnt/nand/linux/android-log.sh /mnt/nand/linux/preinstall.sh.orig \
    /mnt/nand/linux/boot.log
echo "   $(( $(df -m /mnt/nand | awk 'NR == 2 {print $4}') - before )) MB freed"
# last, so a run stopped halfway can be run again
echo 'Android removed by remove-android.sh; --undo puts it back' > /etc/ozone/no-android
sync
EOF
    echo ">> done: restart the box (Downloads and .cache move to their partitions at the start)"
    exit 0
fi

# ---- --undo ----------------------------------------------------------------------------------------

echo ">> Downloads back into debian.img, the partitions unmounted"
ssh_box 'sudo sh -s' << 'EOF'
set -e
H=$(getent passwd 1000 | cut -d: -f6)
for d in "$H/Downloads" /var/cache/apt/archives "$H/.cache"; do
    mountpoint -q "$d" && umount -l "$d"
done
if mountpoint -q /mnt/storage; then
    [ -d /mnt/storage/downloads ] && cp -a /mnt/storage/downloads/. "$H/Downloads"/
    umount -l /mnt/storage
fi
mountpoint -q /mnt/cache && umount -l /mnt/cache
update-rc.d ozone-storage remove > /dev/null
rm -f /etc/ozone/no-android
sync
EOF

echo ">> recovery: Android's boot image"
ssh_box 'sudo dd of=/dev/recovery bs=64k conv=fsync 2> /dev/null' < "$B/boot.img"
same /dev/recovery "$B/boot.img" || die "recovery differs from boot.img: run --undo again"

echo ">> system: Android (1 GB, a few minutes)"
ssh_box 'sudo dd of=/dev/system bs=1M conv=fsync 2> /dev/null' < "$B/system.img"
same /dev/system "$B/system.img" || die "system differs from system.img: run --undo again"

echo ">> cache"
gunzip -c "$B/cache.img.gz" | ssh_box 'sudo dd of=/dev/cache bs=1M conv=fsync 2> /dev/null'

echo ">> Android's files on the data partition"
ssh_box "sudo tar -xzpf - -C /mnt/nand --numeric-owner --xattrs --xattrs-include='*' && sync" < "$B/android-data.tar.gz"
echo ">> done: restart the box; the power menu's Android starts it"
