#!/bin/bash
# Builds btusb.ko, the driver for USB Bluetooth adapters, for the stock 3.10.33 kernel on the
# laptop, into native/out/bluetooth/.
#
# The stock kernel has the whole Bluetooth stack built in (HCI, L2CAP, RFCOMM, BNEP, HIDP, and
# UHID for Bluetooth LE keyboards), but only the UART driver for a chip on the board, which this
# box doesn't have: no driver for USB adapters. The tree and the toolchain are the ones of the GPU
# modules (gpu/build-modules.sh: Hardkernel's 3.10.33 for the same S805, the box's own config).
# The stock kernel has no MODVERSIONS, so only vermagic is checked: the struct layouts were
# compared by hand, the field offsets in hci_alloc_dev, hci_register_dev, hci_recv_frame and the
# rest built from this tree against the stock kernel's code, and they are the same.
set -euo pipefail
cd "$(dirname "$0")"
HERE=$PWD
GPU_CACHE=$HERE/../cache/gpu
CACHE=$HERE/../cache/bluetooth
OUT=$HERE/../out/bluetooth

KERNEL_REV=be4ee634b2cb1e55799b9fc72a4b2ff0e8e164ef   # odroidc-3.10.y-android, 3.10.33 (as the GPU's)
TOOLCHAIN_URL=https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/+archive/refs/heads/lollipop-release.tar.gz

mkdir -p "$GPU_CACHE" "$CACHE" "$OUT"
cd "$GPU_CACHE"

fetch() {  # url file
    [ -s "$2" ] || { echo "Downloading $2"; curl -fL -o "$2.part" "$1"; mv "$2.part" "$2"; }
}

fetch "$TOOLCHAIN_URL" arm-eabi-4.8.tar.gz
[ -x arm-eabi-4.8/bin/arm-eabi-gcc ] || { mkdir -p arm-eabi-4.8; tar xzf arm-eabi-4.8.tar.gz -C arm-eabi-4.8; }
fetch https://codeload.github.com/hardkernel/linux/tar.gz/$KERNEL_REV kernel-$KERNEL_REV.tar.gz

K=$CACHE/linux
rm -rf "$K"
mkdir -p "$K"
echo "Unpacking the kernel"
tar xzf kernel-$KERNEL_REV.tar.gz -C "$K" --strip-components=1
cd "$K"

export PATH=$GPU_CACHE/arm-eabi-4.8/bin:$PATH
M="make ARCH=arm CROSS_COMPILE=arm-eabi- HOSTCFLAGS=-fcommon -j$(nproc)"
cp "$HERE/../gpu/kernel-3.10.33.config" .config
$M olddefconfig > /dev/null
$M modules_prepare > /dev/null
$M M=drivers/bluetooth CONFIG_BT_HCIBTUSB=m modules

arm-eabi-strip --strip-debug -o "$OUT/btusb.ko" drivers/bluetooth/btusb.ko
ls -l "$OUT"
