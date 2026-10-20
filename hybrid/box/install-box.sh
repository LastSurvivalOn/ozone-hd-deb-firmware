#!/system/bin/sh
# Deploys Debian on the box (run as root from install.sh).
# Usage: install-box.sh <files dir> <image size> <user>
set -e

TMP=$1
SIZE=$2
USER_NAME=$3
BB=/system/xbin/busybox
BASE=/data/linux
ROOT=$BASE/root

$BB mkdir -p $BASE $ROOT
$BB install -m 755 $TMP/ozlinux.sh $BASE/ozlinux.sh
$BASE/ozlinux.sh stop >/dev/null 2>&1 || true

if [ ! -f $BASE/debian.img ]; then
    echo "creating $SIZE image"
    make_ext4fs -l $SIZE $BASE/debian.img
    loop=$($BB losetup -f)
    $BB losetup $loop $BASE/debian.img
    $BB mount -t ext4 $loop $ROOT
    echo "unpacking rootfs"
    $BB tar xzf $TMP/rootfs.tar.gz -C $ROOT
    $BB umount $ROOT
    $BB losetup -d $loop
else
    echo "image already exists, keeping it"
fi

$BASE/ozlinux.sh mount
$BB mkdir -p $ROOT/etc/ozone
$BB install -m 755 $TMP/rc $ROOT/etc/ozone/rc
$BB install -m 755 $TMP/debian-setup.sh $ROOT/root/debian-setup.sh
$BB install -m 644 $TMP/user.pub $ROOT/root/user.pub

$BB chroot $ROOT /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root TERM=linux \
    /bin/bash /root/debian-setup.sh $USER_NAME /root/user.pub
$BB rm -f $ROOT/root/debian-setup.sh $ROOT/root/user.pub

$BASE/ozlinux.sh restart
$BASE/ozlinux.sh status
echo OZ_INSTALL_OK
