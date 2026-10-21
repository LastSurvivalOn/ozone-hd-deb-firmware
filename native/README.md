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
./install-gpu.sh <ip>               # Mali-450 GPU for X: modules, X driver, ozone-gl, mpv on the GPU
./install-games.sh <ip>             # RetroArch (NES, GB, GBA) and glmark2, needs the GPU
```

`install-gpu.sh` builds the kernel modules here first if they aren't in `out/gpu/` yet
(`gpu/build-modules.sh`: it downloads a kernel tree and a toolchain into `cache/gpu/`).

Flashing the image (from Android, as root): `dd if=linux-recovery.img of=/dev/block/boot`; before that, Android is
copied to recovery: `dd if=/dev/block/boot of=/dev/block/recovery`. Backups of all partitions are in
`~/ozone_backup/backup/`.

Boot logo: `./install-logo.sh <ip> [1920x1080 png]` (default `logo/bootup.png`) writes the picture into
the logo partition that u-boot shows at power-on and installs the same picture as the Linux splash;
`--stock` puts the original back. `tools/logoimg.py` unpacks and repacks that partition (`AML_RES!`).

Desktop packages:
`xserver-xorg-core xserver-xorg-legacy xserver-xorg-video-fbdev xserver-xorg-input-evdev
xserver-xorg-input-mouse xinit x11-xserver-utils x11-utils x11-xkb-utils xfonts-base bspwm sxhkd polybar rofi
feh lxterminal fish bash-completion fonts-jetbrains-mono fonts-font-awesome fonts-noto-color-emoji mpv alsa-utils
python3 nodejs dbus-x11 python3-gi gir1.2-webkit2-4.1 gir1.2-gtk-3.0 libglib2.0-bin xdotool wpasupplicant` (with `--no-install-recommends`), and to build amplay: `gcc libc6-dev make
pkg-config libavformat-dev libavcodec-dev libswresample-dev libavutil-dev libasound2-dev libx11-dev
libxft-dev`.

## Desktop

How to use all of it, with every key combination: [MANUAL.md](MANUAL.md). What stock Android does and
this doesn't yet, by difficulty: [TODO.md](TODO.md).

bspwm + sxhkd + polybar + rofi, Catppuccin Mocha theme. On the panel: app menu, YouTube,
workspaces, CPU/memory/temperature/IP/volume/clock, power.

| keys | action |
|---|---|
| Super+Enter | terminal |
| Super+D | apps |
| Super+Y | YouTube (search → amplay) |
| Super+Shift+E | power (sleep, Android, reboot, power off) |
| Super+Q / F / S | close / fullscreen / floating |
| Super+1…5 | workspaces |

## WiFi

`ozone-wifi` (the WiFi item on the panel, also in the app menu): the networks around in rofi, a
password the first time, and saved networks connect again by themselves. The chip is a Realtek
8189ES on SDIO, with Android's vendor driver (`8189es.ko`, copied by prepare-debian.sh).
`/etc/init.d/ozone-wifi` starts it the way Android's WiFi HAL does:
1. Power: ioctl `0x6d01` (`_IO('m', 1)`) on `/dev/wifi_power`. The SDIO card appears only after that.
2. `insmod 8189es.ko ifname=wlan0 if2name=p2p0`.
3. `wpa_supplicant` (nl80211) with `/etc/wpa_supplicant/ozone.conf`. It is root's file, and
   passwords are stored as derived keys.
4. A `wpa_cli` action script (`lib/ozone/wifi-event`) runs dhclient on association, with route
   metric 600, so the cable wins when both are connected.

`lib/ozone/netwatch` follows the cable. Kernel 3.10 keeps using a route whose link is down, so when
the cable is pulled out it removes eth0's address and routes, and runs DHCP again when it comes
back. Logs: `/var/log/wpa_supplicant.log`, `/var/log/netwatch.log`.

## Television (DVB-T/T2)

`./install-tv.sh <ip>` (needs the Android NDK here, `~/Android/Sdk/ndk/*`). Super+T or the TV button
on the panel: channel list, "Scan channels" the first time. In the player ←/→ or PgDn/PgUp switch
channels, the rest as in amplay.

```sh
ozone-tv scan                    # UHF 21..60 (474-786 MHz, 8 MHz), prints level and services per multiplex
ozone-tv scan --freq 570000      # one multiplex
ozone-tv status 570000           # signal level/quality once a second: for aiming the antenna
ozone-tv play 3                  # channel 3 of the list
ozone-tv log                     # what the frontend libraries say
ozone-tv --test mux.ts scan      # without an antenna: a TS file fed into the demux from memory
```

How it works. The kernel has the DVB demux but no frontend driver: on Android the TV app
(`th.dtv`) drives the Availink **AVL6762** demodulator and MaxLinear **MxL608** tuner itself, from
its libraries (`libth_dtv_external_c.so` and friends), over `/dev/i2c-2`, demodulator reset on GPIO 74,
TS into demux0 through the parallel `ts0` input. Those libraries, firmware patch included, are
reused as they are:

- `dtv/dtvfe.c` is an Android (bionic) program, built with the NDK for Android 4.4's linker (SysV
  hash, no packed relocations). It `dlopen`s the libraries from `/system/lib` and calls
  `External_Init`, `PI_System_Start`, `PI_Tuner_Init`, then `PI_Tuner_DVB_T_StartLock` /
  `CheckSignalStatus` (interfaces taken from the disassembly), answering `tune`/`status` on stdin.
- `/etc/init.d/ozone-dtv` gives it what Android's init would: `/system` mounted read-only, an
  empty KitKat property area at `/dev/__properties__` (bionic crashes without one), the kernel
  logger nodes in `/dev/log`, and the Android names of the DVB nodes (`/dev/dvb0.demux0`...).
- `ozone-tv` reads PAT/SDT/NIT/PMT through demux section filters (names usually ISO 8859-5, LCN from
  NIT), then for watching sets TS-tap PES filters on the service's PIDs and pipes `dvr0` into
  `amplay --live --program <sid>`.

The TV window (`ozone-tv`, GTK) has the channels with what's on, and the selected channel's guide. The guide is the
broadcast EIT, read by one section filter on PID 0x12 (tables 0x4e-0x6f) whenever something is tuned:
Zeonbud's multiplexes carry now/next and a week of schedule for their own services and for the
others', so watching or scanning fills in most of it. `ozone-tv guide` spends 25 s on each
multiplex for the rest. It is kept in `~/.local/share/ozone-tv/epg.json`, and the TDT (PID 0x14)
corrects the clock if the box's is off. In the player, Enter opens amplay's channel list on the left (`--channels`:
number, name, now on); choosing one prints `channel N` and exits with status 12, and ozone-tv retunes. Channels are
numbered by their place in the list, as the stock app does: the NIT gives the same LCNs to services of different
multiplexes.

Not done: teletext/subtitles, recording, radio services, descrambling (channels found encrypted get a lock).

Logs, for debugging without a network: every `ozone-tv` run writes `/mnt/nand/linux/logs/tv-<time>-<command>/tv.log`
(what it did, the frontend's replies, the libraries' Android log, the kernel log, amplay, the signal
every 3 s), and a scan adds 3 s of each multiplex found (`mux-<MHz>.ts`, replay with `--test`).
Under Android, `hybrid/box/android-log.sh` (installed at `/data/linux/android-log.sh`, started by
`ozlinux.sh start` at boot) records logcat, the kernel log and the DVB sysfs state into
`/data/linux/logs/android-N/`, i.e. `/mnt/nand/linux/logs/` seen from native Linux.

## YouTube

`yt` (Super+Y, the YouTube button on the panel, and what `ozone-web` hands YouTube over to) is a
GTK window with a grid of results: thumbnail, duration (or LIVE), channel avatar and name, views,
upload date. Type to search, arrows and Enter to play, Esc to close; more results load at the end
of the list, and the window comes back after the video. `yt URL` plays right away. Next to the
search: the highest quality and frame rate to play (kept in `~/.config/ozone/yt.json`), for a slow
connection; the player's title shows what was picked (`1080p60`). YouTube has 60 fps videos at
30 fps only up to 480p, so 30 fps means 480p for them.

The search goes straight to YouTube's web API (`youtubei/v1/search`, the JSON the site loads
itself): ~1.3 s for 20 results, where yt-dlp needs ~10 s on this CPU (4.6 s of it just starting up)
and has no avatars. If the API reply stops parsing, `yt` falls back to yt-dlp's flat search with the
same fields except avatars. Playing still goes through yt-dlp (`-g`, H.264 up to 1080p), so a
video takes ~10 s to start. Thumbnails are the plain `mqdefault.jpg`: the URLs in the results serve
WebP, which this gdk-pixbuf can't load. `YT_HL`/`YT_GL` set the language and region of the
results (default `en`/`UA`).

## Browser

`ozone-web` (Super+W, the globe on the panel): Python + GTK3 on WebKitGTK 2.50, with tabs, an
address/search bar (DuckDuckGo HTML), find (Ctrl+F), zoom, downloads to `~/Downloads`, session
restore. One instance: opening it again adds a tab, and it starts hidden with the desktop
(`--background`, ~60 MB) with a web process already running, so the panel button shows it in
~0.1 s and a page like HN is up in ~2 s; closing the window frees the tabs and hides it again.
Measured on the box before picking WebKit:

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
- ads and trackers blocked by a WebKit content filter (pgl.yoyo.org, `ozone-web --update-filters`),
  and third-party scripts blocked except for library CDNs: CNN loads in 21 s instead of 45, The Verge
  in 12 instead of 48, pravda.com.ua in 3.6 instead of 8. The shield button lets them through for a
  site that breaks without them (remembered);
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
| no udev to mount drives | `ozone-media` (init script, native only) reads kernel uevents from netlink and mounts USB/SD drives in `/media`: vfat, NTFS (ntfs-3g), exFAT (exfat-fuse), ext, iso9660/udf |
| the kernel's CIFS speaks SMB1 only (`vers=2.x/3.0`: "Unknown vers= option"), and `mount.cifs` drops its capabilities, so `ANDROID_PARANOID_NETWORK` refuses its socket (error 13) | network folders go over SMB2/3 with rclone from rclone.org (Debian's 1.60 creates a folder named "．" in the share); kernel CIFS (with the `aid_inet` group) only for SMB1-only servers |
| `mpv --ao=alsa` enables silence padding → the HDMI audio driver hangs the kernel | `ao=sdl` |
| no volume control, no mmap (dmix) | softvol `Master`, one sound at a time |
| no `getrandom()`/seccomp | Dropbear instead of OpenSSH, chrony with `-F 0` |
| no fbcon | the boot logo stays on screen until X starts; ozone-hw redraws it on fb0 and pulses it |
| power off reboots: `meson_power_off` restarts with reason `charging_reboot`, u-boot boots on | `HALT=halt` (and `INIT_HALT`) in `/etc/default/halt`; the diverted `/etc/init.d/halt` ends in deep suspend instead (below), halting only if that fails |
| no GPU in X: the Mali X11 driver needs UMP and DRM, the stock kernel has neither, and the Mali libraries for Linux are fbdev-only | our own modules (UMP, Mali r5p0 with UMP, DRM, `mali_drm`, `fbump`) built for the stock kernel from the box's own config, and the Mali X driver on Xorg 21 with a cached shadow ([gpu/README.md](gpu/README.md)) |
| `rmmod mali` oopses (`aml_thermal` keeps calling the gone GPU cooling device) and `panic_on_oops` reboots | our `mali.ko` refuses to unload; never unload Android's either |
| deep suspend powers USB down, wakes only on the IR remote's Power (red LED meanwhile), and waking is a cold boot | it is power off: after SysRq `u` (the data partition under `debian.img` stays writable past umountroot); Sleep = Android early suspend with a wakelock held (`ozone-standby`): HDMI off, any key wakes |

## GPU

The Mali-450 MP4 draws OpenGL ES 2.0 under X: `./install-gpu.sh`, details in
[gpu/README.md](gpu/README.md).

- At boot `ozone-gpu` loads the modules, and the desktop gets the Mali X driver. If the modules
  aren't there, or that X crashes, the desktop gets the plain framebuffer.
- `touch /etc/ozone/no-gpu` turns it off.
- Programs use the GPU through `ozone-gl PROGRAM`; mpv and RetroArch do it by themselves.
- `ozone-gl` only changes the library path. Without it, a program gets Mesa in software, as before.
- Your own code builds with `pkg-config --cflags --libs mali-gles`.

| | |
|---|---|
| GLES window, 1280x720 full screen | 120 fps (pure GPU fill: 280) |
| RetroArch, full-screen NES | 82 fps |
| glmark2-es2 | 81 |
| a 3x3 blur, 720p RGBA, GPU round trip | 27 ms against 180 on the CPU |

The browser can render on it: `OZONE_WEB_GPU=1 ozone-web`, through the shims in
[gpu/egl-fix](gpu/egl-fix). CSS animations run 8 times faster. But every frame then crosses to X
whole, about 25 a second at most, so plain wheel scrolling gains nothing (and memory goes down by
~45 MB). That is why it's off by default.

## Video

`amplay` (source in `amplay/`) plays through the S805 hardware decoder: H.264 and HEVC up to 1080p60,
MPEG-1/2. FFmpeg demuxes (files, HTTP, separate video and audio streams) and decodes audio for ALSA; the
elementary video stream goes to Amlogic's amstream driver with a PTS per packet, and the decoder puts
frames straight on the video layer, so the CPU stays at ~20-40 %. Under X the player is a window in
fb0's hardware color key (#000008 is transparent: a bright key showed as fringes where the scaler
blends edges), so the video shows through it and the controls are plain X drawing on top: title
and close at the top, buttons, time, a clickable progress bar and volume at the bottom. They hide after 3 s; Xorg stays at ~0 % CPU. Without X the desktop is blanked
and keys are read from evdev.

```sh
amplay film.mkv
amplay --audio <audio url> <video url>      # what yt does with YouTube's DASH streams
amplay --reset                              # brings the desktop back if amplay was killed
```

Keys: Space pause, ←/→ ±10 s, PgDn/PgUp ±60 s, ↑/↓ volume, M mute, Q/Esc quit; mouse: move to show
the controls, click the picture to pause, the progress bar to seek, wheel for volume. `yt` (Super+Y) picks
H.264 up to 1080p and falls back to mpv, which decodes in software (360p at best). Without the GPU,
converting frames to RGB565 caps it at ~20 fps. With the GPU, mpv scales and converts on the Mali
(`/usr/local/bin/mpv`), at 111 % CPU for 360p instead of 190.

How the driver is driven, and what breaks it:

- Port setup as the stock Android player does it: `VFORMAT`, `VID`, `SYSINFO`, `PORT_INIT` on
  `/dev/amstream_vbuf` (`_hevc` for HEVC), `/dev/amvideo` held open; then `TSTAMP` (90 kHz) before each
  packet. Without PTS the decoder runs but amvideo never shows a frame.
- `/sys/class/tv/policy_fr_auto` must be 0 (ozone-hw sets it at boot, amplay checks): with frame-rate
  automation on, closing the decoder dereferences a NULL mode name in `tv_validate_vmode` and the
  kernel oopses.
- tsync is always in audio master mode after port init, and audio isn't fed to the DSP, so pcrscr is
  driven from userspace: the kernel sets it 3 s behind the first frame (its wait for audio); amplay
  starts the clock at that moment and keeps pcrscr within 40 ms of the ALSA clock. After a pause the
  kernel moves pcrscr on by the paused time, so amplay checks it every 20 ms for a second after.
- Frames that reach the decoder after their time are all dropped, and with nothing on the video
  layer the screen shows the color key. YouTube's video comes over its own connection,
  1080p60 at 3-7 Mbit/s over WiFi that does ~6, so it can fall behind the audio; amplay then
  holds the clock ("Loading…") until 3 s of video are downloaded ahead.
