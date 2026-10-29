# OzoneHD T2 → Debian 12

Scripts that turn an OzoneHD T2 WiFi set-top box (Amlogic S805, Android 4.4) into a Debian 12
media centre run with the TV remote: the box's own DVB-T2 tuner, hardware video decoding and the
Mali GPU on its stock kernel, with Android kept for dual boot.

The box's 3.10 vendor kernel stays: NAND, WiFi and the T2 tuner have no mainline drivers. A custom
initramfs boots Debian from an image file on the data partition, and everything Android did is
done again in Linux (programs of this repository's own, in Python and C).

## What it does

- **Television (DVB-T2):** channel scans, favourites, the guide, recording to USB, timeshift
  (pausing live TV), timers, subtitles, teletext, radio, parental lock. The tuner runs through the
  stock TV app's own libraries (`native/dtv`).
- **Video and music:** a player on the hardware decoder (H.264, HEVC, MPEG-2; up to 1080p),
  automatic frame rate, audio passthrough, an equalizer, a music player, photos and a slideshow
  screensaver.
- **YouTube** without a browser, with history, recommendations and playlists kept on the box, and
  casting from the YouTube app on a phone (its Cast button).
- **A browser** (WebKitGTK, on the GPU), with video handed to the hardware decoder.
- **Network:** WiFi, PPPoE, static addresses, a WiFi hotspot, VPN (OpenVPN, WireGuard,
  L2TP/IPsec, PPTP), network folders (SMB), sharing the box's files (Samba, a DLNA server).
- **Phones:** Miracast screen mirroring, a DLNA renderer, a remote control and keyboard in the
  phone's browser.
- **Desktop:** bspwm, polybar and rofi, made for the remote (on-screen keyboard, mouse mode, hints
  on every screen), Bluetooth with a USB adapter, gamepads, emulators and 3D games through gl4es.
- **System:** updates from GitHub and Debian, backups, a factory reset, a programs manager, a
  calendar, a calculator with Maxima, a code editor, sleep and standby.

Not possible on this hardware: 4K (the S805 outputs 1080p at most), Netflix and other Widevine
DRM, Chromium (it can't start on kernel 3.10), systemd and Docker (no cgroups or namespaces).

## Hardware

| | |
|---|---|
| SoC | Amlogic S805 (Meson8B), 4 × Cortex-A5, ARMv7 with NEON, 32-bit |
| GPU | Mali-450 MP2 |
| RAM | 1 GB (~830 MB for Linux) |
| Storage | 8 GB NAND behind a vendor driver |
| Network | 100 Mbit Ethernet, Realtek 8189ES WiFi |
| Tuner | AVL6762 + MxL608 (DVB-T/T2) |
| Kernel | stock Amlogic 3.10.33 |

Debian here is `armhf`. Programs built for arm64 / aarch64 or x86 won't run, nor will Android
apps: those need a reboot into Android.

## Two ways to run it

Both use the same Debian image, `/data/linux/debian.img`.

| | [`hybrid/`](hybrid) | [`native/`](native) |
|---|---|---|
| What boots | Android, with Debian in a chroot beside it | Debian, from the boot partition |
| Android | runs all the time | one reboot away (`reboot-android`) |
| For | a quiet Debian server next to Android | the media centre above |

## Getting started

Everything runs from a Linux computer on the same network as the box, over `adb` and `ssh`.

1. **Back up every partition first** (boot, recovery, system, logo, misc, cache, data). The
   scripts read the backups from `~/ozone_backup/backup/`. They are the only way back.
2. Copy `.env.example` to `.env` and fill in the box's address and your user name.
3. Install Debian into the image from Android: [`hybrid/install.sh`](hybrid/install.sh)
   ([hybrid/README.md](hybrid/README.md)).
4. For the native system, build the boot image, prepare Debian and add the parts you want:
   ```sh
   cd native
   ./build-recovery.sh               # out/linux-recovery.img from the stock boot.img
   ./prepare-debian.sh               # sysvinit, network, modules, hardware
   ./install-desktop.sh              # the desktop, the player, YouTube
   ./install-gpu.sh                  # the Mali GPU
   ./install-android-files.sh        # the Android libraries the tuner needs, from your backup
   ./install-tv.sh                   # the T2 tuner
   ./install-network.sh              # the Network menu, VPN, sharing
   ```
   Then flash it from Android as root: copy `boot` to `recovery`, then write the image to
   `boot`. The order, the packages and the optional parts are in
   [native/README.md](native/README.md#building-and-installing).
5. After that the box updates itself from this repository's `main` branch.

> **Warning.** This writes to the boot partitions of a device with no official recovery tool.
> The scripts never touch the bootloader. Several kernel constraints were found by bricking or
> hanging the box: read [CLAUDE.md](CLAUDE.md) ("Kernel and hardware constraints") before
> changing anything low-level. If Linux doesn't start, a plain reboot lands in Android, and u-boot
> can still load `recovery.img` from an SD card or a USB stick.

## Documentation

| File | What's in it |
|---|---|
| [native/MANUAL.md](native/MANUAL.md) | the user manual: the remote, every program, all key combinations |
| [native/README.md](native/README.md) | how the native system is built and how each part works |
| [native/gpu/README.md](native/gpu/README.md) | the Mali-450: kernel modules, the X driver, EGL fixes, our own WebKit, gl4es |
| [hybrid/README.md](hybrid/README.md) | Debian beside Android |
| [native/TODO.md](native/TODO.md) | what stock Android has that isn't done yet |
| [CLAUDE.md](CLAUDE.md) | the kernel and hardware constraints, for anyone changing the code |

## License

GPL-3.0-or-later, with a permission to combine it with the Mali and stock Android libraries. A
few directories keep the licenses of the code they change. See [LICENSING.md](LICENSING.md). No
vendor binaries are included: the scripts copy them from the box or the user's own backup.
