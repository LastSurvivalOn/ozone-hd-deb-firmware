#!/bin/bash
# Builds out/linux-recovery.img: stock kernel and device tree from boot.img + our initramfs.
# Usage: ./build-recovery.sh [path to stock boot.img]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
STOCK=${1:-$HOME/ozone_backup/backup/boot.img}
BUSYBOX=$HERE/cache/busybox
W=$HERE/out/work

[ -x "$BUSYBOX" ] || { echo "missing $BUSYBOX (static armhf busybox from the busybox-static package)"; exit 1; }

rm -rf "$W" && mkdir -p "$W"
python3 -I "$HERE/tools/bootimg.py" unpack "$STOCK" "$W/stock"

# The NAND driver comes from the stock ramdisk: it is built for exactly this kernel
mkdir -p "$W/stock-rd"
zcat "$W/stock/ramdisk" | (cd "$W/stock-rd" && cpio -id --quiet --no-absolute-filenames boot/aml_nftl_dev.ko)

cat > "$W/initramfs.spec" <<EOF
dir  /bin 755
dir  /dev 755
dir  /lib 755
dir  /lib/modules 755
dir  /proc 755
dir  /sys 755
dir  /tmp 1777
dir  /nand 755
dir  /newroot 755
nod  /dev/console 600 c 5 1
nod  /dev/null 666 c 1 3
file /init 755 $HERE/initramfs/init
file /bin/busybox 755 $BUSYBOX
file /bin/udhcpc.script 755 $HERE/initramfs/udhcpc.script
file /lib/modules/aml_nftl_dev.ko 644 $W/stock-rd/boot/aml_nftl_dev.ko
EOF
python3 -I "$HERE/tools/mkcpio.py" "$W/initramfs.spec" "$W/initramfs.cpio.gz"
python3 -I "$HERE/tools/bootimg.py" pack "$W/stock" "$W/initramfs.cpio.gz" "$HERE/out/linux-recovery.img"

ls -l "$HERE/out/linux-recovery.img"
md5sum "$HERE/out/linux-recovery.img"
