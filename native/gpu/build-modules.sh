#!/bin/bash
# Builds the GPU kernel modules for the stock 3.10.33 kernel on the laptop:
# ump.ko, umplock.ko, mali.ko (Mali-450 r5p0 with UMP), hdmi.ko, drm.ko,
# mali_drm.ko and fbump.ko, into native/out/gpu/.
#
# The stock kernel has no UMP and no DRM, so the stock mali.ko can't serve the
# Mali X11 driver. Sources: Hardkernel's ODROID-C1 kernel (the same S805), the
# 3.10.33 Android branch for the headers and the Linux branch for Mali r5p0
# (API 600, as Android's own mali.ko); the config is the box's own, taken out
# of the stock boot.img (kernel-3.10.33.config). The stock kernel has no
# MODVERSIONS, so only vermagic is checked: struct layouts are on us, and the
# OS layer of the result matches the stock mali.ko instruction for instruction.
set -euo pipefail
cd "$(dirname "$0")"
GPU=$PWD
CACHE=$GPU/../cache/gpu
OUT=$GPU/../out/gpu

KERNEL_REV=be4ee634b2cb1e55799b9fc72a4b2ff0e8e164ef   # odroidc-3.10.y-android, 3.10.33
MALI_REV=d34598be186f970324513a5344e110dbb01f008c     # odroidc-3.10.y, last Mali r5p0
TOOLCHAIN_URL=https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/+archive/refs/heads/lollipop-release.tar.gz

mkdir -p "$CACHE" "$OUT"
cd "$CACHE"

fetch() {  # url file
    [ -s "$2" ] || { echo "Downloading $2"; curl -fL -o "$2.part" "$1"; mv "$2.part" "$2"; }
}

fetch "$TOOLCHAIN_URL" arm-eabi-4.8.tar.gz
[ -x arm-eabi-4.8/bin/arm-eabi-gcc ] || { mkdir -p arm-eabi-4.8; tar xzf arm-eabi-4.8.tar.gz -C arm-eabi-4.8; }
fetch https://codeload.github.com/hardkernel/linux/tar.gz/$KERNEL_REV kernel-$KERNEL_REV.tar.gz
fetch https://codeload.github.com/hardkernel/linux/tar.gz/$MALI_REV kernel-$MALI_REV.tar.gz

# A fresh tree every time, so the patches always apply to pristine sources
K=$CACHE/kernel/linux
rm -rf "$CACHE/kernel"
mkdir -p "$K"
echo "Unpacking the kernel"
tar xzf kernel-$KERNEL_REV.tar.gz -C "$K" --strip-components=1
tar xzf kernel-$MALI_REV.tar.gz -C "$K" --strip-components=1 \
    --wildcards '*/drivers/gpu/arm' '*/include/ump' '*/drivers/gpu/drm/mali_drm'
cd "$K"
for p in "$GPU"/kernel/patches/*.patch; do patch -s -p1 < "$p"; done
mkdir -p ozone
cp -r "$GPU/kernel/fbump" "$GPU/kernel/hdmi" ozone/
ln -s ../../drivers/video/hdmi.c ozone/hdmi/hdmi.c
# drivers/amlogic/Kconfig looks for Mali outside the tree, as in Amlogic's SDK
mkdir -p ../hardware/arm/gpu
ln -s ../../../linux/drivers/gpu/arm/mali ../hardware/arm/gpu/mali
ln -s ../../../linux/drivers/gpu/arm/ump ../hardware/arm/gpu/ump

export PATH=$CACHE/arm-eabi-4.8/bin:$PATH
M="make ARCH=arm CROSS_COMPILE=arm-eabi- HOSTCFLAGS=-fcommon -j$(nproc)"
cp "$GPU/kernel-3.10.33.config" .config
# Mali450 as the hardware is; DVFS off as in the stock module (the meson
# platform scales through its own utilization callback); UMP for X11; DRM for
# DRI2 (it pulls in CONFIG_HDMI, which the stock kernel lacks: hdmi.ko)
scripts/config -e MALI450 -d MALI_DVFS -m UMP -d UMP_DEBUG -e MALI400_UMP -m DRM
$M olddefconfig > /dev/null
$M modules_prepare > /dev/null

UMP_SYMS=$K/drivers/gpu/arm/ump/Module.symvers
$M M=drivers/gpu/arm/ump modules
$M M=drivers/gpu/arm/umplock CONFIG_UMPLOCK=m modules
$M M=drivers/gpu/arm/mali KBUILD_EXTRA_SYMBOLS=$UMP_SYMS modules
$M M=ozone/hdmi modules
$M M=drivers/gpu/drm modules
$M M=drivers/gpu/drm/mali_drm CONFIG_DRM_MALI=m KBUILD_EXTRA_SYMBOLS=$K/drivers/gpu/drm/Module.symvers modules
$M M=ozone/fbump KBUILD_EXTRA_SYMBOLS=$UMP_SYMS modules

for m in drivers/gpu/arm/ump/ump.ko drivers/gpu/arm/umplock/umplock.ko drivers/gpu/arm/mali/mali.ko \
         ozone/hdmi/hdmi.ko drivers/gpu/drm/drm.ko drivers/gpu/drm/mali_drm/mali_drm.ko ozone/fbump/fbump.ko; do
    arm-eabi-strip --strip-debug -o "$OUT/$(basename $m)" $m
done
ls -l "$OUT"
