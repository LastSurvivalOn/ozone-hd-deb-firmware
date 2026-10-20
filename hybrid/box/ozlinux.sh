#!/system/bin/sh
# Debian alongside Android: mounts the /data/linux/debian.img image and starts services in it.
# Usage: ozlinux.sh start | stop | restart | shell | status | mount

BASE=/data/linux
IMG=$BASE/debian.img
ROOT=$BASE/root
BB=/system/xbin/busybox
HOSTNAME=ozone

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export HOME=/root LANG=C.UTF-8 TERM=${TERM:-linux}
unset LD_LIBRARY_PATH LD_PRELOAD BOOTCLASSPATH
# Android's su runs with umask 0, so services would create files with mode 666
umask 022

mounted() { $BB grep -q " $1 " /proc/mounts; }

in_root() { $BB chroot $ROOT "$@"; }

do_mount() {
    if ! mounted $ROOT; then
        $BB mkdir -p $ROOT
        loop=$($BB losetup -f) || return 1
        $BB losetup $loop $IMG || return 1
        $BB mount -t ext4 -o noatime $loop $ROOT || { $BB losetup -d $loop; return 1; }
    fi

    # Bionic's /dev lacks these links, but bash/coreutils rely on them
    for n in fd:/proc/self/fd stdin:/proc/self/fd/0 stdout:/proc/self/fd/1 stderr:/proc/self/fd/2; do
        [ -e /dev/${n%%:*} ] || $BB ln -s ${n#*:} /dev/${n%%:*}
    done
    $BB mkdir -p /dev/shm

    mounted $ROOT/dev      || $BB mount -o bind /dev $ROOT/dev
    mounted $ROOT/dev/pts  || $BB mount -o bind /dev/pts $ROOT/dev/pts
    mounted $ROOT/dev/shm  || $BB mount -t tmpfs -o mode=1777,size=64m tmpfs $ROOT/dev/shm
    mounted $ROOT/proc     || $BB mount -t proc proc $ROOT/proc
    mounted $ROOT/sys      || $BB mount -t sysfs sysfs $ROOT/sys
    mounted $ROOT/run      || $BB mount -t tmpfs -o mode=755,size=16m tmpfs $ROOT/run
    $BB mkdir -p $ROOT/mnt/sdcard
    mounted $ROOT/mnt/sdcard || $BB mount -o bind /data/media/0 $ROOT/mnt/sdcard

    # Take DNS from Android, since it updates it when the network changes
    $BB rm -f $ROOT/etc/resolv.conf
    for d in $(/system/bin/getprop net.dns1) $(/system/bin/getprop net.dns2) 1.1.1.1; do
        echo "nameserver $d"
    done > $ROOT/etc/resolv.conf
    echo $HOSTNAME > /proc/sys/kernel/hostname
}

# All processes whose root is our chroot
chroot_pids() {
    for p in /proc/[0-9]*; do
        [ "$($BB readlink $p/root 2>/dev/null)" = "$ROOT" ] && echo ${p#/proc/}
    done
}

do_umount() {
    pids=$(chroot_pids)
    [ -n "$pids" ] && kill $pids 2>/dev/null && $BB sleep 3
    pids=$(chroot_pids)
    [ -n "$pids" ] && kill -9 $pids 2>/dev/null && $BB sleep 1
    for m in mnt/sdcard run sys proc dev/shm dev/pts dev; do
        mounted $ROOT/$m && $BB umount $ROOT/$m
    done
    if mounted $ROOT; then
        loop=$($BB grep " $ROOT " /proc/mounts | $BB cut -d' ' -f1)
        $BB umount $ROOT && $BB losetup -d $loop
    fi
}

case "$1" in
mount)
    do_mount
    ;;
start)
    # Android-side logs for the TV work (android-log.sh), independent of Debian
    [ -f /data/linux/android-log.sh ] && $BB setsid /system/bin/sh /data/linux/android-log.sh > /dev/null 2>&1 &
    do_mount || { echo "failed to mount $IMG"; exit 1; }
    # Below the lowmemorykiller threshold (it only touches >= 0), but the kernel OOM killer can still kill it
    echo -100 > /proc/self/oom_score_adj
    in_root /etc/ozone/rc start
    ;;
stop)
    mounted $ROOT && in_root /etc/ozone/rc stop
    do_umount
    ;;
restart)
    $0 stop; $0 start
    ;;
shell)
    do_mount || exit 1
    in_root /bin/bash -l
    ;;
status)
    if mounted $ROOT; then
        echo "mounted: $ROOT"
        echo "processes in Debian: $(chroot_pids | $BB wc -l)"
    else
        echo "not mounted"
    fi
    ;;
*)
    echo "usage: $0 start|stop|restart|shell|status|mount"
    exit 1
    ;;
esac
