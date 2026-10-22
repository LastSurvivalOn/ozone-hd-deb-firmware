#!/bin/bash
# Installs Debian 12 (armhf) into the /data/linux/debian.img image on the box via adb.
# Usage: ./install.sh [image size, default 2G]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
CACHE=$HERE/cache
SIZE=${1:-2G}
. "$HERE/../config.sh"
USER_NAME=$BOX_USER
KEY=$SSH_KEY
TMP=/data/local/tmp/ozinst

fetch_rootfs() {
    # Official debian:bookworm-slim for arm/v7 straight from Docker Hub, without docker
    local reg=https://registry-1.docker.io/v2/library/debian tok idx man layer
    tok=$(curl -fsS "https://auth.docker.io/token?service=registry.docker.io&scope=repository:library/debian:pull" |
          python3 -I -c 'import sys,json; print(json.load(sys.stdin)["token"])')
    idx=$(curl -fsS -H "Authorization: Bearer $tok" \
          -H "Accept: application/vnd.oci.image.index.v1+json,application/vnd.docker.distribution.manifest.list.v2+json" \
          $reg/manifests/bookworm-slim)
    man=$(python3 -I -c 'import sys,json
d=json.loads(sys.argv[1])
print([m["digest"] for m in d["manifests"] if m["platform"].get("architecture")=="arm" and m["platform"].get("variant")=="v7"][0])' "$idx")
    layer=$(curl -fsS -H "Authorization: Bearer $tok" \
          -H "Accept: application/vnd.oci.image.manifest.v1+json,application/vnd.docker.distribution.manifest.v2+json" \
          $reg/manifests/$man | python3 -I -c 'import sys,json; print(json.load(sys.stdin)["layers"][0]["digest"])')
    curl -fsSL -H "Authorization: Bearer $tok" $reg/blobs/$layer -o $CACHE/rootfs.tar.gz.part
    echo "${layer#sha256:}  $CACHE/rootfs.tar.gz.part" | sha256sum -c --quiet
    mv $CACHE/rootfs.tar.gz.part $CACHE/rootfs.tar.gz
}

mkdir -p $CACHE
[ -f $CACHE/rootfs.tar.gz ] || { echo ">> downloading Debian bookworm-slim armv7"; fetch_rootfs; }
[ -f $KEY ] || ssh-keygen -t ed25519 -N '' -C "$USER_NAME@ozone" -f $KEY

adb get-state >/dev/null

echo ">> copying files to the box"
adb shell "rm -rf $TMP && mkdir -p $TMP"
adb push $CACHE/rootfs.tar.gz $HERE/box/ozlinux.sh $HERE/box/rc $HERE/box/debian-setup.sh $HERE/box/install-box.sh $TMP/
adb push $KEY.pub $TMP/user.pub

echo ">> installing on the box"
# adb on Android 4.4 does not return the exit code, so wait for a marker at the end
adb shell "su -c 'sh $TMP/install-box.sh $TMP $SIZE $USER_NAME'" | tee /dev/stderr | grep -q OZ_INSTALL_OK ||
    { echo "!! installation did not finish"; exit 1; }
adb shell "rm -rf $TMP"

echo ">> done. Login: ssh -i $KEY $USER_NAME@<box ip>"
