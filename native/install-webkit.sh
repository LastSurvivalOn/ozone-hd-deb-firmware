#!/bin/bash
# Installs our WebKitGTK (webkit/build.sh: Debian's 2.50.6 with webkit/patches) on the box, in
# /usr/local/lib/ozone-webkit next to Debian's, which stays as it is. Only ozone-web with the GPU
# (OZONE_WEB_GPU=1) uses it, through LD_LIBRARY_PATH, with its frames as X pixmaps
# (WEBKIT_X11_PIXMAP_RENDERER). OZONE_WEB_OWN_WEBKIT=0 or removing the directory goes back to
# Debian's. Rebuild after every webkit2gtk security update: it is a copy of a fixed version.
# Usage: ./install-webkit.sh <box ip>
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
IP=${1:?specify the box IP}
KEY=$HOME/.ssh/id_ed25519_ozone
ssh_box() { ssh -i $KEY -o ConnectTimeout=10 <USER>@$IP "$@"; }
PREFIX=/usr/local/lib/ozone-webkit
STAGE=$HERE/cache/webkit/install$PREFIX

echo ">> build (only what changed) and stage"
"$HERE/webkit/build.sh" install > /dev/null
[ -e "$STAGE/lib/libwebkit2gtk-4.1.so.0" ] || { echo "nothing staged in $STAGE" >&2; exit 1; }

echo ">> to the box"
# the libraries, their helper processes (libexec) and the injected bundle; no headers or tools
(cd "$STAGE" && tar --owner=0 --group=0 -czf - lib/libwebkit2gtk-4.1.so* lib/libjavascriptcoregtk-4.1.so* \
    lib/webkit2gtk-4.1 libexec) |
    ssh_box "sudo mkdir -p $PREFIX && sudo tar -C $PREFIX --no-same-owner -xzf - && sync && du -sh $PREFIX"
tar -C "$HERE/rootfs" -czf - usr/local/bin/ozone-web | ssh_box 'sudo tar -C / --no-same-owner -xzf - && sync'
echo ">> done: OZONE_WEB_GPU=1 ozone-web (an ozone-web already running keeps its WebKit until it quits)"
