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

Boot logo: `./install-logo.sh <ip> [1920x1080 png]` (default `logo/bootup.png`) writes the picture into
the logo partition that u-boot shows at power-on and installs the same picture as the Linux splash;
`--stock` puts the original back. `tools/logoimg.py` unpacks and repacks that partition (`AML_RES!`).

Desktop packages:
`xserver-xorg-core xserver-xorg-legacy xserver-xorg-video-fbdev xserver-xorg-input-evdev
xserver-xorg-input-mouse xinit x11-xserver-utils x11-utils xfonts-base bspwm sxhkd polybar rofi
feh rxvt-unicode fonts-jetbrains-mono fonts-font-awesome fonts-noto-color-emoji mpv alsa-utils
python3 nodejs dbus-x11 python3-gi gir1.2-webkit2-4.1 gir1.2-gtk-3.0` (with `--no-install-recommends`), and to build amplay: `gcc libc6-dev make
pkg-config libavformat-dev libavcodec-dev libswresample-dev libavutil-dev libasound2-dev libx11-dev
libxft-dev`.

## Desktop

bspwm + sxhkd + polybar + rofi, Catppuccin Mocha theme. On the panel: app menu, YouTube,
workspaces, CPU/memory/temperature/IP/volume/clock, power.

| keys | action |
|---|---|
| Super+Enter | terminal |
| Super+D | apps |
| Super+Y | YouTube (search → mpv) |
| Super+Shift+E | power (sleep, Android, reboot, power off) |
| Super+Q / F / S | close / fullscreen / floating |
| Super+1…5 | workspaces |

## Browser

`ozone-web` (Super+W, the globe on the panel): Python + GTK3 on WebKitGTK 2.50, with tabs, an
address/search bar (DuckDuckGo HTML), find (Ctrl+F), zoom, downloads to `~/Downloads`, session
restore. One instance: opening it again adds a tab. Measured on the box before picking it:

| | Chromium 150 | Firefox ESR 153 | WebKitGTK 2.50 |
|---|---|---|---|
| starts | no: killed in `execve` on 3.10 | 27-33 s | 9-13 s (1.5 s as a new tab) |
| scrolling a plain page | | 27 fps | 2.7 fps by default, **45 fps** with GPU paths off |
| memory | | 280-400 MB | 150-250 MB |

On real sites the limit is their JavaScript, not painting: wheel scrolling Wikipedia ran at 1-3 fps,
BBC at 0.5, and with JavaScript off at 16-20 and 12 (composited mode, CPU/llvmpipe raster, dropping
shadows and filters, unfixing sticky headers: no gain). So:

- **fast mode** (⚡, F8): JavaScript off for the site, remembered in `~/.local/share/ozone-web/sites.json`;
  on by default for sites that read fine without it (Wikipedia, BBC, Stack Exchange, HN, old.reddit…);
- **reader view** (📖, F9): the article as a static page, ~1 s to open, scrolls at 20 fps;
- ads and trackers blocked by a WebKit content filter (pgl.yoyo.org, `ozone-web --update-filters`);
- no HTML5 media: YouTube watch pages (also the in-app navigation) go to `yt`/amplay, YouTube
  search to `yt`;
- `WEBKIT_DISABLE_DMABUF_RENDERER`/`WEBKIT_DISABLE_COMPOSITING_MODE`, no smooth scrolling, WebGL,
  WebAudio; 350 MB memory limit per web process; zram swap (384 MB) from `ozone-hw`;
- the CPU governor is `interactive` with all four cores (`ozone-hw`): the stock `hotplug` keeps one
  core and made pages load 20-35 % slower.

Load log (times per page, crashes): `~/.cache/ozone-web/log`.

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
| no fbcon | the boot logo stays on screen until X starts; ozone-hw redraws it on fb0 and pulses it |
| power off reboots: `meson_power_off` restarts with reason `charging_reboot`, u-boot boots on | `HALT=halt` (and `INIT_HALT`) in `/etc/default/halt`, HDMI off first; plug in to turn on |
| deep suspend powers USB down, wakes only on the IR remote | Sleep = Android early suspend with a wakelock held (`ozone-standby`): HDMI off, any key wakes |

## Video

`amplay` (source in `amplay/`) plays through the S805 hardware decoder: H.264 and HEVC up to 1080p60,
MPEG-1/2. FFmpeg demuxes (files, HTTP, separate video and audio streams) and decodes audio for ALSA; the
elementary video stream goes to Amlogic's amstream driver with a PTS per packet, and the decoder puts
frames straight on the video layer, so the CPU stays at ~20-40 %. Under X the player is a window in
fb0's hardware color key (magenta is transparent), so the video shows through it and the controls
are plain X drawing on top: title and close at the top, buttons, time, a clickable progress bar and
volume at the bottom. They hide after 3 s; Xorg stays at ~0 % CPU. Without X the desktop is blanked
and keys are read from evdev.

```sh
amplay film.mkv
amplay --audio <audio url> <video url>      # what yt does with YouTube's DASH streams
amplay --reset                              # brings the desktop back if amplay was killed
```

Keys: Space pause, ←/→ ±10 s, PgDn/PgUp ±60 s, ↑/↓ volume, M mute, Q/Esc quit; mouse: move to show
the controls, click the picture to pause, the progress bar to seek, wheel for volume. `yt` (Super+Y) picks
H.264 up to 1080p and falls back to mpv, which decodes in software (360p at best: converting frames
to RGB565 caps it at ~20 fps).

How the driver is driven, and what breaks it:

- Port setup as the stock Android player does it: `VFORMAT`, `VID`, `SYSINFO`, `PORT_INIT` on
  `/dev/amstream_vbuf` (`_hevc` for HEVC), `/dev/amvideo` held open; then `TSTAMP` (90 kHz) before each
  packet. Without PTS the decoder runs but amvideo never shows a frame.
- `/sys/class/tv/policy_fr_auto` must be 0 (ozone-hw sets it at boot, amplay checks): with frame-rate
  automation on, closing the decoder dereferences a NULL mode name in `tv_validate_vmode` and the
  kernel oopses.
- tsync is always in audio master mode after port init, and audio isn't fed to the DSP, so pcrscr is
  driven from userspace: the kernel sets it 3 s behind the first frame (its wait for audio); amplay
  starts the clock at that moment and keeps pcrscr within 40 ms of the ALSA clock.
