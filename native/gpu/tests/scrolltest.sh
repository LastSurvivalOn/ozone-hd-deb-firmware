#!/bin/sh
# Run on the box from /var/tmp/mali, with screenfps.py and long.html next to it.
# scrolltest.sh MODE URL CLICKS INTERVAL: ozone-web wheel scrolling, screen changes a second
mode=$1; url=$2; clicks=${3:-60}; gap=${4:-0.1}
cd /var/tmp/mali
for p in $(pgrep -f "^/usr/bin/python3 /usr/local/bin/ozone-we[b]"); do kill $p; done
sleep 3
case $mode in
cpu) set -- ;;
debian) set -- OZONE_WEB_GPU=1 OZONE_WEB_OWN_WEBKIT=0 ;;
pixmap) set -- OZONE_WEB_GPU=1 ;;
esac
(DISPLAY=:0 env "$@" setsid ozone-web "$url" > /tmp/ow-$mode.log 2>&1 &)
sleep 20
DISPLAY=:0 xdotool mousemove 640 400
secs=$(python3 -c "print(round($clicks * $gap, 1))")
(python3 screenfps.py $secs 60,140 60,192 60,244 60,296 60,348 60,400 60,452 60,504 60,556 60,608 200,140 200,192 200,244 200,296 200,348 200,400 200,452 200,504 200,556 200,608 340,140 340,192 340,244 340,296 340,348 340,400 340,452 340,504 340,556 340,608 480,140 480,192 480,244 480,296 480,348 480,400 480,452 480,504 480,556 480,608 620,140 620,192 620,244 620,296 620,348 620,400 620,452 620,504 620,556 620,608 760,140 760,192 760,244 760,296 760,348 760,400 760,452 760,504 760,556 760,608 > /tmp/sf.txt &)
(top -b -d 3 -n 2 > /tmp/top-scroll.txt &)
t0=$(date +%s.%N); DISPLAY=:0 xdotool click --repeat $clicks --delay $(python3 -c "print(int($gap * 1000))") 5; t1=$(date +%s.%N)
sleep 1
echo "$mode $(basename $url) $clicks clicks every ${gap}s ($(python3 -c "print(round($clicks / ($t1 - $t0), 1))") a second): $(cat /tmp/sf.txt) | $(awk '/^top -/{k++} k==2 && /WebKitWeb|Xorg|python3/ {printf "%s %s%%  ", $12, $9}' /tmp/top-scroll.txt)"
