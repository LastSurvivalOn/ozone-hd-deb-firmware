#!/bin/sh
pkill -x polybar
# a polybar whose X went away can hang while shutting down: give it 3 s
n=15
while pgrep -x polybar > /dev/null; do
    n=$((n - 1))
    [ $n -le 0 ] && pkill -9 -x polybar
    sleep 0.2
done
# the whole bar on a 1280-wide desktop, a shorter one on the smaller desktops of a bigger interface,
# the shortest on the 4:3 one (736)
w=$(xdpyinfo 2> /dev/null | awk '/dimensions:/ { split($2, a, "x"); print a[1] }')
bar=main
[ "${w:-1280}" -lt 1280 ] && bar=compact
[ "${w:-1280}" -lt 900 ] && bar=narrow
polybar $bar > /tmp/polybar.log 2>&1 &
