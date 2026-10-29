#!/bin/bash
# Builds xfrm_user.ko for the stock 3.10.33 kernel on the laptop, into native/out/vpn/.
#
# The stock kernel has IPsec built in (XFRM, ESP, AH, the tunnel and transport modes, PF_KEY for
# Android's racoon) but not XFRM_USER, the netlink interface that strongSwan's charon talks to the
# kernel through: without it charon has only its user-space IPsec (kernel-libipsec), which can't
# do the transport mode L2TP/IPsec needs. The module uses only what the kernel exports for it
# (xfrm_register_km, xfrm_state_lookup, xfrm_cfg_mutex... all in the stock kernel's ksymtab).
# The tree and toolchain are the GPU modules' and btusb's (Hardkernel's 3.10.33 for the S805,
# the box's own config); only vermagic is checked (no MODVERSIONS).
set -euo pipefail
cd "$(dirname "$0")"
HERE=$PWD
GPU_CACHE=$HERE/../cache/gpu
K=$HERE/../cache/bluetooth/linux   # build-btusb.sh's tree
OUT=$HERE/../out/vpn
mkdir -p "$OUT"

[ -d "$K/net/xfrm" ] && [ -x "$GPU_CACHE/arm-eabi-4.8/bin/arm-eabi-gcc" ] || "$HERE/../bluetooth/build-btusb.sh"
cd "$K"
export PATH=$GPU_CACHE/arm-eabi-4.8/bin:$PATH
M="make ARCH=arm CROSS_COMPILE=arm-eabi- HOSTCFLAGS=-fcommon -j$(nproc)"
cp "$HERE/../gpu/kernel-3.10.33.config" .config
$M olddefconfig > /dev/null
$M modules_prepare > /dev/null
$M M=net/xfrm CONFIG_XFRM_USER=m modules
arm-eabi-strip --strip-debug -o "$OUT/xfrm_user.ko" net/xfrm/xfrm_user.ko
ls -l "$OUT"
