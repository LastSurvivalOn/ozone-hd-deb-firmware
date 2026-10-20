#!/bin/bash
# Enables Debian autostart: appends an ozlinux.sh call to the stock /system/bin/preinstall.sh,
# which init runs as root after every boot (when the boot animation ends).
# Usage: ./autostart.sh          — enable
#        ./autostart.sh --undo   — restore the original preinstall.sh
set -euo pipefail

HOOK='/system/xbin/busybox setsid /system/bin/sh /data/linux/ozlinux.sh start > /data/linux/boot.log 2>&1 &  # ozlinux'
F=/system/bin/preinstall.sh
ORIG=/data/linux/preinstall.sh.orig

root() { adb shell "su -c \"$*\"" | tr -d '\r'; }

root "mount -o remount,rw /system"
if [ "${1:-}" = "--undo" ]; then
    root "[ -f $ORIG ] && cat $ORIG > $F"
else
    root "[ -f $ORIG ] || cp $F $ORIG"
    # Always start from the original, so re-running doesn't duplicate the line.
    # The stock file has no trailing \n, so separate the line first
    root "cat $ORIG > $F; echo >> $F; echo '$HOOK' >> $F"
fi
root "mount -o remount,ro /system"
root "tail -3 $F"
