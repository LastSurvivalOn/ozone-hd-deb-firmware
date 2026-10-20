# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Scripts that turn an OzoneHD T2 WiFi set-top box (Amlogic S805 / Meson8B, armv7, Android 4.4, stock kernel 3.10.33) into a Debian 12 armhf machine. They run from the laptop and reach the box over `adb` and `ssh`. There is no build system or test suite. Everything is bash, busybox `sh` (on the box) and a few dependency-free Python 3 tools. The only way to verify a change is to deploy it to the real box.

Everything (READMEs, comments, script messages, UI labels) is in English.

- Box: `ssh -i ~/.ssh/id_ed25519_ozone <USER>@<BOX_IP>` (passwordless sudo), `adb connect <BOX_IP>:5555` (rooted, `su`).
- Partition backups (boot, recovery, system, misc, logo; **not** the bootloader) live in `ozone_backup/backup/` and also in `~/ozone_backup/backup/`, which the scripts read by default.
- `*/cache/` holds downloaded artifacts (Debian rootfs tarball, SmartTube apk, static armhf busybox). `native/out/` is build output.

## Two phases, one Debian image

Both phases share **the same** Debian rootfs: an ext4 file at `/data/linux/debian.img` on the NAND `data` partition. Any change to files inside Debian must work in both modes.

### `hybrid/`: Debian in a chroot under Android
- `install.sh [size]`: downloads `debian:bookworm-slim` arm/v7 straight from the Docker Hub registry API (no docker), pushes it with `box/*` through adb, then runs `box/install-box.sh` as root on the box. That script creates the image and chroots in to run `box/debian-setup.sh` (groups, user, Dropbear, sysv-rc).
- `box/ozlinux.sh start|stop|restart|shell|status|mount` (installed at `/data/linux/ozlinux.sh`): loop-mounts the image, bind-mounts `/dev`, `/proc` and the rest, copies DNS from Android props, then runs `/etc/ozone/rc`.
- `box/rc`: a minimal init replacement that runs `/etc/rc2.d/S*` in order and **skips** the services listed in `/etc/ozone/native-only`.
- `autostart.sh`: appends the `ozlinux.sh start` hook to stock `/system/bin/preinstall.sh`, which init runs as root after boot. The original is kept at `/data/linux/preinstall.sh.orig`.
- `debloat.sh`: runs `pm disable` on a package list and installs SmartTube. `--undo` reverses it.
- Android 4.4 `adb shell` does not return exit codes, so scripts print a marker (`OZ_INSTALL_OK`) and grep for it.

### `native/`: pure Linux booted by the stock kernel (dual boot)
- `build-recovery.sh [stock boot.img]` → `out/linux-recovery.img`. It takes the stock kernel and header from `boot.img` (via `tools/bootimg.py unpack/pack`, Android boot image v0), extracts `aml_nftl_dev.ko` from the stock ramdisk, and builds a new initramfs with `tools/mkcpio.py` from a spec file, without root or fakeroot. Needs `cache/busybox` (static armhf).
- `initramfs/init`: insmod the NAND driver → mount `/dev/data` → loop-mount `debian.img` → `switch_root` to sysvinit. On failure it saves a log to `/data/linux/native-boot.log`, opens telnet on eth0 for 5 minutes, then reboots, and a plain reboot lands in Android.
- Partition layout: the **boot** partition holds the Linux image (default boot), and **recovery** holds the stock Android `boot.img`. `reboot-android` writes `/run/ozone-reboot-arg`. The diverted `/etc/init.d/reboot` then calls `reboot(RESTART2, "recovery")` via a perl syscall. This relies on the stock script having been diverted by hand to `/usr/local/lib/ozone/reboot.distrib`, and no script in this repo performs that `dpkg-divert`.
- `prepare-debian.sh <ip>`: copies kernel modules from Android (`8189es.ko`, `aml_thermal.ko`, `mali.ko`) into `/lib/modules/3.10.33/extra`, untars `rootfs/` over `/`, installs sysvinit-core, chrony and related packages.
- `install-desktop.sh <ip>`: deploys a subset of `rootfs/` plus `dotfiles/` (bspwm, sxhkd, polybar, rofi; Catppuccin Mocha), builds `amplay/` on the box, fetches yt-dlp, and adds an X respawn line to `/etc/inittab`. Desktop packages are installed manually first; the list is in `native/README.md`.
- `rootfs/usr/local/bin/ozone-web`: the browser (Python, GTK3, WebKit2 4.1), see `native/README.md`.
- `amplay/amplay.c`: video player for the hardware decoder (amstream ioctls, FFmpeg demux, ALSA audio, evdev keys). Built natively on the box with gcc; there is no cross toolchain. `yt` uses it and falls back to mpv.
- `install-logo.sh <ip>`: repacks the `logo` partition (`tools/logoimg.py`, AML_RES v2) with `logo/bootup.png` (1920x1080) and installs the same picture as `/usr/share/ozone/splash.rgb565`, which `ozone-hw` draws on fb0 and `lib/ozone/splash` pulses via the OSD global alpha ioctl until X starts.
- `rootfs/` mirrors absolute paths on the box. `/etc/ozone/is-native` tells the modes apart: it exits 0 when PID 1 is sysvinit and 1 under Android (`/init`). Hardware and input init scripts guard on it so they do nothing in chroot mode.
- Flashing is done from Android as root, manually: `dd if=linux-recovery.img of=/dev/block/boot`, after first copying boot → recovery.

## Kernel and hardware constraints

Several of these were found by bricking or hanging the box. Don't undo them.

- **No systemd, ever.** The kernel has no cgroups, namespaces or overlayfs. apt can quietly swap `sysvinit-core` for `systemd-sysv` through dependencies (e.g. `libpam-systemd`), which causes a boot loop. `rootfs/etc/apt/preferences.d/ozone-no-systemd` pins it at -1. Use `--no-install-recommends` and `dbus-x11`. After any package install, check that `/sbin/init` is still sysvinit.
- **`ANDROID_PARANOID_NETWORK`**: only gid 3003 (`aid_inet`) may open sockets. Daemons that drop supplementary groups (`_apt`, `_chrony`) need `aid_inet` as their *primary* group.
- No `getrandom()` and no seccomp, so Dropbear replaces OpenSSH and chrony runs with `DAEMON_OPTS="-F 0"`.
- `HUNG_TASK_PANIC` and `SOFTLOCKUP_PANIC` are on. `rootfs/etc/sysctl.d/ozone.conf` disables them, as Android does.
- Display: fb0 must be **16 bpp RGB565**. The 32-bit OSD formats have alpha and X writes 0, which gives a black screen; Xorg 21 rejects 24 bpp. `ozone-hw` sets this up. There is no fbcon, so nothing appears on screen before X starts.
- **mpv must use `ao=sdl`.** `ao=alsa` sets `silence_size=boundary`, and the Amlogic HDMI audio driver then hangs the kernel. ALSA uses softvol `Master` with no dmix (no mmap).
- No working udev. Device permissions are set in `ozone-hw`. Xorg input comes from a generated static config (`gen-xorg-input`). Keyboard hotplug goes through `kbd-merge`, a python-evdev bridge that creates the `ozone-kbd` virtual device through the legacy uinput API, because 3.10 has no `UI_DEV_SETUP`. X only knows `ozone-kbd` from when it started, so `kbd-merge` must never exit (on 3.10 even `close()` of an unplugged device fails with ENODEV; that used to kill it and leave X without a keyboard). Errors go to `/var/log/kbd-merge.log`. Restarting it means restarting X too (`bspc quit`).
- Mainline Linux or Armbian is not an option: NAND sits behind the proprietary `aml_nftl_dev.ko`, WiFi needs the vendor `8189es.ko`, and DVB-T2 tuning runs in userspace Android libs.
- **Power off can't cut power.** `meson_power_off` reboots (reason `charging_reboot`), so `/etc/default/halt` forces `INIT_HALT=HALT` and `ozone-hw stop` blanks HDMI at runlevel 0. Deep suspend (`echo mem` without a wakelock) powers USB down and only the IR remote wakes it; the user has no remote. `ozone-standby` holds a wakelock so only Android's early suspend runs.
- **Hardware decoder:** `/sys/class/tv/policy_fr_auto` is 1 after boot and must be 0, or closing a decoder oopses the kernel (NULL mode name in `tv_validate_vmode`). Every packet needs `AMSTREAM_IOC_TSTAMP`, or no frame is shown. tsync is forced to audio master by port init and sets pcrscr 3 s behind the first frame; amplay drives pcrscr itself. Stream the kernel log over ssh (`dmesg -w`) when testing: oopses reboot the box and the log is lost.
- **Browser:** Chromium can't run (SIGKILL in `execve` on 3.10). WebKitGTK needs `WEBKIT_DISABLE_DMABUF_RENDERER=1` (otherwise it renders via software EGL at ~3 fps). On heavy sites page JavaScript, not painting, limits scrolling, which is why `ozone-web` has per-site JS off ("fast mode") and a reader view. Measure scrolling with real wheel events (`xdotool click 5`) and on-screen frame changes; a JS `scrollBy` loop measures something else.
- The CPU governor is `interactive` with all cores online (`ozone-hw`); the stock `hotplug` was slower for interactive use.
- Run `sync` after deploying files to the box. A crash has left a 0-byte init script before.
- `/system` is nearly full and is mounted read-only. Remount it rw only briefly, as `autostart.sh` does.
