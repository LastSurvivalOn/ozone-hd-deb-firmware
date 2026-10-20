# OzoneHD T2: pure Linux (dual boot with Android)

Stock Amlogic 3.10 kernel + custom initramfs + Debian 12 (sysvinit) from the file
`/data/linux/debian.img`, the same one that runs under Android in chroot mode (see `../hybrid`).

```
boot partition      → Linux (out/linux-recovery.img)      ← boots by default
recovery partition  → stock Android boot.img (T2, SmartTube)
```

- Linux → Android: `reboot-android` or the power menu on the panel.
- Android → Linux: any normal reboot or power cycle.
- Login: `ssh -i ~/.ssh/id_ed25519_ozone <USER>@<BOX_IP>`

## Building and installing

```sh
./build-recovery.sh                 # out/linux-recovery.img from the stock boot.img
./prepare-debian.sh <ip>            # sysvinit, network, time, modules, hardware, systemd ban
# desktop packages — see below
./install-desktop.sh <ip>           # bspwm/polybar/rofi, yt, X autostart
```

Flashing the image (from Android, as root): `dd if=linux-recovery.img of=/dev/block/boot`; before that, Android is
copied to recovery: `dd if=/dev/block/boot of=/dev/block/recovery`. Backups of all partitions are in
`~/ozone_backup/backup/`.

Desktop packages:
`xserver-xorg-core xserver-xorg-legacy xserver-xorg-video-fbdev xserver-xorg-input-evdev
xserver-xorg-input-mouse xinit x11-xserver-utils x11-utils xfonts-base bspwm sxhkd polybar rofi
feh rxvt-unicode fonts-jetbrains-mono fonts-font-awesome fonts-noto-color-emoji mpv alsa-utils
python3 nodejs dbus-x11` (with `--no-install-recommends`).

## Desktop

bspwm + sxhkd + polybar + rofi, Catppuccin Mocha theme. On the panel: app menu, YouTube,
workspaces, CPU/memory/temperature/IP/volume/clock, power.

| keys | action |
|---|---|
| Super+Enter | terminal |
| Super+D | apps |
| Super+Y | YouTube (search → mpv) |
| Super+Shift+E | power (Android, reboot) |
| Super+Q / F / S | close / fullscreen / floating |
| Super+1…5 | workspaces |

## Hardware and kernel limitations (already handled)

| problem | solution |
|---|---|
| no cgroups → systemd doesn't work; apt silently installed `systemd-sysv` in place of sysvinit (boot loop) | sysvinit + `/etc/apt/preferences.d/ozone-no-systemd` |
| `HUNG_TASK_PANIC`/`SOFTLOCKUP_PANIC` in the kernel: slow NAND crashed the system | `/etc/sysctl.d/ozone.conf`, as in Android |
| 32-bit OSD formats have alpha, X writes 0 → black screen; Xorg doesn't support 24 bits | 16-bit framebuffer (RGB565) |
| udev doesn't work on 3.10 | input config is generated before X starts; device permissions are set in `ozone-hw` |
| `mpv --ao=alsa` enables silence padding → the HDMI audio driver hangs the kernel | `ao=sdl` |
| no volume control, no mmap (dmix) | softvol `Master`, one sound at a time |
| no `getrandom()`/seccomp | Dropbear instead of OpenSSH, chrony with `-F 0` |
| no fbcon | only the logo is on screen until X starts |

## Video

The S805 hardware decoder is only reachable through Amlogic's closed interface (amstream), which mpv
doesn't support. In software: 360p decodes at ~65 fps, but converting frames for the screen limits
playback to ~20 fps without audio and ~14 fps with audio. For 1080p YouTube use Android + SmartTube.
