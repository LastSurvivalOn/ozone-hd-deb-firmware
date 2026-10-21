#!/bin/sh
pkill -x polybar
# a polybar whose X went away can hang while shutting down: give it 3 s
n=15
while pgrep -x polybar > /dev/null; do
    n=$((n - 1))
    [ $n -le 0 ] && pkill -9 -x polybar
    sleep 0.2
done
polybar main > /tmp/polybar.log 2>&1 &
