#!/system/bin/sh
# Records what Android does with the TV hardware, to compare with native Linux (ozone-tv): the
# whole Android log (the TV app th.dtv and its frontend libraries log there), the kernel log, the
# system properties, and the DVB/decoder state from sysfs whenever it changes. Needs no network:
# everything goes to /data/linux/logs/android-N/ (from native Linux: /mnt/nand/linux/logs/).
# Started at boot by ozlinux.sh start (installed at /data/linux/android-log.sh); the last 5 boots
# are kept.
# ozlinux.sh runs this with Debian's PATH, where Android's tools are not
export PATH=/system/bin:/system/xbin
BB=/system/xbin/busybox
D=/data/linux/logs
mkdir -p $D
n=1
while [ -e $D/android-$n ]; do n=$((n + 1)); done
L=$D/android-$n
mkdir $L
# the oldest go
set -- $($BB ls -d $D/android-* | $BB sort -t- -k2 -n)
while [ $# -gt 5 ]; do
    $BB rm -rf $1
    shift
done

{
    echo "boot $n, $($BB date), uptime $(cat /proc/uptime)"
    cat /proc/version
} > $L/info.txt
getprop > $L/props.txt
dmesg > $L/dmesg-boot.txt

# 8 files of 4 MB, rotated by logcat itself
logcat -b main -b system -b radio -v threadtime -f $L/logcat.txt -r 4096 -n 8 &
# the kernel log from here on, at most 64 MB
$BB setsid $BB sh -c "cat /proc/kmsg | $BB head -c 67108864 > $L/kmsg.txt" &

# The DVB and decoder state, written out when it changes (checked every 2 s)
STATE="/sys/class/stb/source /sys/class/stb/demux0_source /sys/class/stb/demux1_source
/sys/class/stb/demux2_source /sys/class/stb/tso_source /sys/class/stb/asyncfifo0_source
/sys/class/stb/asyncfifo1_source /sys/class/stb/dsc_source /sys/class/stb/hw_setting
/sys/class/stb/demux0_channel_activity /sys/class/stb/demux0_filter_users
/sys/class/stb/demux0_free_filters /sys/class/stb/dvr0_mode /sys/class/tsync/mode
/sys/class/tsync/enable /sys/class/tv/policy_fr_auto /sys/class/display/mode
/sys/class/video/frame_width /sys/class/video/frame_height /sys/class/video/disable_video"
last=
while true; do
    now=$(for f in $STATE; do [ -r $f ] && echo "$f: $(cat $f 2>&1 | $BB head -c 300)"; done
          echo "processes: $(ps | $BB grep -iE 'dtv|mediaserver' | $BB awk '{print $2, $NF}' | $BB tr '\n' ' ')")
    if [ "$now" != "$last" ]; then
        { echo "=== $($BB date +%H:%M:%S) (uptime $($BB cut -d' ' -f1 /proc/uptime))"; echo "$now"; } >> $L/state.txt
        last=$now
    fi
    sync
    sleep 2
done
