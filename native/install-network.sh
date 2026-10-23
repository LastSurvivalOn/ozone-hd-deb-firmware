#!/bin/bash
# Installs the network extras on top of the desktop (install-desktop.sh): the Network menu (a fixed
# address or PPPoE on the cable, a fixed address on WiFi, WPS, a proxy), the WiFi hotspot, VPN
# (OpenVPN, and WireGuard in user space), the remote control on a phone, and DLNA (sending to the
# box from a phone, and browsing the media servers on the network).
# Usage: ./install-network.sh [box ip]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../config.sh"
IP=$(box_ip "${1:-}")

echo ">> wireguard-go (kernel 3.10 has no WireGuard), built here"
[ -x "$HERE/out/wireguard-go" ] || "$HERE/vpn/build-wireguard-go.sh"

echo ">> packages"
ssh_box 'set -e
    # pppoe: the kernel has PPP but no PPPoE, so the user-space one runs under pppd
    sudo apt-get install -y -qq --no-install-recommends iw hostapd dnsmasq-base iptables openvpn ppp pppoe \
        wireguard-tools qrencode > /dev/null
    sudo apt-get clean
    # the hotspot and VPN start their own daemons, when chosen in the menu
    for s in hostapd openvpn; do [ -e /etc/init.d/$s ] && sudo update-rc.d $s disable > /dev/null 2>&1 || true; done
    dpkg -S /sbin/init | grep -q sysvinit-core || { echo "!! /sbin/init is not sysvinit any more" >&2; exit 1; }'

echo ">> files"
cat "$HERE/out/wireguard-go" | ssh_box 'sudo install -m 755 /dev/stdin /usr/local/bin/wireguard-go'
tar -C "$HERE/rootfs" -czf - usr/local/bin/ozone-network usr/local/bin/ozone-wifi usr/local/bin/ozone-hotspot \
    usr/local/bin/ozone-vpn usr/local/bin/ozone-phone usr/local/bin/ozone-dlna usr/local/bin/ozone-remote \
    usr/local/bin/ozone-files usr/local/bin/yt usr/local/bin/ozone-web usr/local/lib/ozone/net-apply \
    usr/local/lib/ozone/netwatch usr/local/lib/ozone/wifi-event usr/local/lib/ozone/vpn-updown \
    usr/local/share/applications/ozone-network.desktop usr/local/share/applications/ozone-dlna.desktop |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'
tar -C "$HERE/dotfiles" -czf - .config/bspwm/bspwmrc .config/polybar/config.ini .config/rofi/config.rasi | ssh_box 'tar -C ~ -xzf -'

echo ">> amplay (--control, for DLNA), built on the box"
tar -C "$HERE/amplay" -czf - amplay.c osd.c osd.h ttx.c ttx.h Makefile |
    ssh_box 'set -e; d=$(mktemp -d); tar -C $d -xzf -; make -s -C $d; sudo make -s -C $d install; rm -r $d'
# netwatch again, now applying the cable's settings (its init script would restart WiFi too)
ssh_box '/etc/ozone/is-native || exit 0
    sudo start-stop-daemon --stop --quiet --pidfile /run/ozone-netwatch.pid --remove-pidfile || true
    sudo start-stop-daemon --start --quiet --background --make-pidfile --pidfile /run/ozone-netwatch.pid \
        --exec /bin/sh -- /usr/local/lib/ozone/netwatch
    sync'
echo ">> done: the Network menu is in the apps menu and on the panel's address; restart the desktop"
echo "   (bspc quit) for the phone remote and DLNA"
