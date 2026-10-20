#!/bin/sh
xrdb -merge ~/.Xresources
xsetroot -cursor_name left_ptr
# The TV must not blank on idle
xset s off -dpms
exec bspwm
