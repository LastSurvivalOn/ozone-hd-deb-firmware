# OzoneHD T2: pure Linux (dual boot with Android)

Stock Amlogic 3.10 kernel + custom initramfs + Debian 12 (sysvinit) from the file
`/data/linux/debian.img`, the same one that runs under Android in chroot mode (see `../hybrid`).

```
boot partition      → Linux (out/linux-recovery.img)      ← boots by default
recovery partition  → stock Android boot.img (T2, SmartTube)
```

- Linux → Android: `reboot-android` or the power menu on the panel.
- Android → Linux: any normal reboot or power cycle.
- Login: `ssh -i ~/.ssh/id_ed25519_ozone <USER>@<BOX_IP>`. The scripts here take the box's
  address, user and key from `../.env` (a copy of `../.env.example`), or the IP as their first
  argument.

## Building and installing

```sh
./build-recovery.sh                 # out/linux-recovery.img from the stock boot.img
./prepare-debian.sh <ip>            # sysvinit, network, time, modules, hardware, systemd ban
# desktop packages — see below
./install-desktop.sh <ip>           # bspwm/polybar/rofi, yt, X autostart
./install-gpu.sh <ip>               # Mali-450 GPU for X: modules, X driver, ozone-gl, mpv on the GPU
./install-games.sh <ip>             # RetroArch (NES, GB, GBA) and glmark2, needs the GPU
./install-network.sh <ip>           # Network menu, hotspot, VPN, phone remote, DLNA
./install-miracast.sh <ip>          # screen mirroring (a patched wpa_supplicant, built on the box)
./install-bluetooth.sh <ip>         # Bluetooth with a USB adapter: btusb.ko, BlueZ, BlueALSA, the menu
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
python3 nodejs dbus-x11 python3-gi gir1.2-webkit2-4.1 gir1.2-gtk-3.0 libglib2.0-bin xdotool wpasupplicant` (with `--no-install-recommends`;
install-desktop.sh adds the equalizer's `libasound2-plugin-equal caps`), and to build amplay: `gcc libc6-dev make
pkg-config libavformat-dev libavcodec-dev libswresample-dev libavutil-dev libasound2-dev libx11-dev
libxft-dev libzvbi-dev`.

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
back (dhclient itself: `ifdown` takes the link down, which looks like the cable going out). Once the
cable has had an address for two checks (6 s), it disconnects WiFi (`wpa_cli disconnect`), and
reconnects it when the cable goes, unless WiFi was off from the WiFi window already. `wifi-event`
removes wlan0's address on any disconnect: on the same subnet as the cable, its route took the
replies to what came in over the cable. Logs: `/var/log/wpa_supplicant.log`, `/var/log/netwatch.log`.

## Network menu, hotspot, VPN, phones, DLNA

`install-network.sh` adds them (packages: `iw hostapd dnsmasq-base iptables openvpn ppp pppoe
wireguard-tools qrencode`; the hostapd and openvpn init scripts are disabled, the tools start their
own daemons). The menus run as the user and call themselves through `sudo -n` for what needs
root; sudo's `secure_path` has no `/usr/local/bin`, so they pass their full path.

- **`ozone-network`** (rofi; the panel's address and WiFi open it) keeps its settings in
  `/etc/ozone/network.json` (root, 600: the PPPoE password), and `lib/ozone/net-apply` applies them:
  netwatch calls it when the cable comes in or out (and at start for a static address or PPPoE),
  wifi-event on WiFi (dis)association. DHCP is dhclient with `IF_METRIC` (100 cable, 600 WiFi);
  static is `ip addr`/`ip route` and resolv.conf. **PPPoE**: the kernel has PPP but no
  `CONFIG_PPPOE`, so it is pppd with the user-space `pppoe` as its pty (`/var/log/pppoe.log`); the
  password goes through `pap-secrets`/`chap-secrets`. The **proxy** goes to `/etc/ozone/proxy`
  (read by yt and ozone-web at start: WebKit gets it through its network proxy settings),
  `/etc/environment` (the next session) and apt. **WPS** is in `ozone-wifi`: `wpa_cli wps_pbc` or
  `wps_pin any`.
- **`ozone-hotspot`**: hostapd on `p2p0`, the 8189ES's second interface, which is a phy of its
  own and does AP mode while `wlan0` stays a client (wlan0 as a fallback, the box's WiFi off
  meanwhile). 192.168.43.1/24; dnsmasq hands out addresses and forwards DNS, running as
  `nobody:aid_inet` (only that group may open sockets). NAT with **`iptables-legacy`**: the
  nf_tables one fails on 3.10 ("Could not fetch rule set generation id"). Settings in
  `/etc/ozone/hotspot.json` (root), log `/var/log/hotspot.log`.
- **`ozone-vpn`**: OpenVPN and WireGuard files in `/etc/ozone/vpn` (root, 700). OpenVPN runs as a
  daemon with `lib/ozone/vpn-updown` for the pushed DNS servers (`/run` is noexec, so the script
  can't live there). WireGuard: no kernel module, so `wireguard-go` (`vpn/build-wireguard-go.sh`,
  Go cross-built here: static ARMv7, 2.7 MB), then `wg setconf` with what wg-quick would do by hand
  (addresses, MTU, a host route to the endpoint unless it is on the LAN, `0/1` and `128/1`, DNS).
  wireguard-go forks itself into the background by adding `WG_PROCESS_FOREGROUND=1` to its
  environment, but the first copy of a variable wins: with `WG_PROCESS_FOREGROUND=0` already set,
  every child forks another. Log `/var/log/vpn.log`. Tested against OpenVPN (TLS, redirect-gateway)
  and WireGuard servers in docker on the laptop: with a server on the LAN, OpenVPN's host route to
  it via the router loses the tunnel (a test artefact, not a real setup).
- **Phone remote**: `ozone-remote` serves a page on port 8080 (private and loopback addresses
  only): `POST /key` (remote.conf's key names, pressed and released, repeats while held), `/move`,
  `/click` (its mouse device) and `/type` (xdotool type). `ozone-phone show` is the address with a
  QR code (`qrencode`).
- **`ozone-dlna`**: a UPnP MediaRenderer (SSDP on every LAN address, HTTP on port 49494: the
  descriptions, SOAP for AVTransport / RenderingControl / ConnectionManager, GENA events with
  LastChange). Videos go to `amplay --control` (commands `pause`, `play`, `seek S`, `quit` on stdin;
  `pos S DURATION playing|paused|loading` every second on stdout), mpv (IPC) when amplay exits 3,
  and for music and photos. Volume is the Master control. `ozone-dlna browse` is a ContentDirectory
  client in rofi (SSDP search for MediaServer, Browse in pages of 200), Files' "Media servers"
  place opens it. Settings `~/.config/ozone/dlna.json`, log `~/.cache/ozone-dlna.log`.

## Screen mirroring (Miracast)

`ozone-miracast` is a Wi-Fi Display sink. WiFi Direct runs on `p2p0` in a wpa_supplicant of its
own (`/usr/local/sbin/wpa_supplicant-p2p`, control socket for the user's group in
`/run/ozone-miracast/ctrl`), with the WFD device information "primary sink, RTSP 7236". The box
only listens (Extended Listen Timing, 300 ms of every 400, on channel 1), stops when a phone starts
connecting, and wants to own the group (`p2p_go_intent=14`); `wpa_cli -a` runs `ozone-miracast` as
root at each group's start and end, for dnsmasq on the group (we own it) or dhclient with
`lib/ozone/miracast-dhcp` (no routes from the phone). Then it is the RTSP client of the phone's port
7236 (M1-M7: H.264 constrained baseline in the CEA modes up to 1080p60, AAC; keep-alives; IDR
requests), takes the RTP on UDP 1028 into segment files in `/dev/shm`, reads the PAT and PMT from
them, and `amplay --tv --rate 30` plays them as live TV: 0.3-0.5 s from arrival to the screen.
`touch /run/ozone-miracast-debug` before opening it logs wpa_supplicant's debug output in
`/var/log/miracast.log`; the session's log is `~/.cache/ozone-miracast.log`.

Debian's wpa_supplicant can't do it with the vendor 8189ES driver; `miracast/build-wpa.sh` builds
the same version with Debian's patches and `miracast/patches/wpa-rtw-p2p.patch`:
- the driver reports the start of a remain-on-channel but never its end, so P2P waited for ever
  after its first listen: the end is made up when the time is over or after a cancel;
- it never passes probe requests up and answers them itself, as a group owner it builds beacons and
  association responses too, all with the WPS and P2P IEs of the Android private command
  `SET_AP_WPS_P2P_IE` (now `set_ap_wps_ie`): without them no phone found the box or joined its group;
- it deauthenticates a removed station with reason 3 ("leaving") whatever the reason (3.10's
  `DEL_STATION` has none), and after WPS a client takes that for the group's end: a WPS enrollee
  gets 500 ms to leave by itself, as it does, before the station is removed.

Tested with the laptop as the source (NetworkManager's WiFi P2P with a WFD IE, a small RTSP source
and ffmpeg's `rtp_mpegts`): found in 2-10 s, the box's group formed 8 times in 9 (the first after
start once failed), picture in 5-10 s from choosing it. As a client of the source's group the box
was refused by the laptop's group owner after WPS one time in two, so it owns the group. Not tried
with a real phone or Windows here; no HDCP, no UIBC (the phone's touch from the TV), no LPCM audio.

## Bluetooth (a USB adapter)

The box has no Bluetooth of its own (`hw.has.bluetooth=false`), but the stock kernel has the whole
stack built in: HCI, L2CAP, RFCOMM, BNEP, HIDP (classic keyboards, mice, gamepads), UHID (Bluetooth
LE ones), RFKILL, the crypto for LE pairing. Only a driver for USB adapters is missing:
`bluetooth/build-btusb.sh` builds `btusb.ko` from the GPU's kernel tree and toolchain (no
MODVERSIONS, so vermagic only: the struct offsets in the Bluetooth core's functions built from this
tree, `hci_alloc_dev`, `hci_register_dev`, `hci_recv_frame` and the rest, match the stock kernel's
code). `/etc/init.d/ozone-bluetooth` loads it at boot (an adapter plugged in later is taken at
once) and runs BlueALSA; bluetoothd is Debian's. Tested with a CSR8510 A10 (`0a12:0001`), the
common cheap one; others that btusb knows should work, Realtek's (RTL8761B and the like) need
firmware loading this btusb doesn't have.

- **bluetoothd** (BlueZ 5.66, management API 1.3 on this kernel) needs `Channels = 1` under
  `[GATT]` in `/etc/bluetooth/main.conf`: Enhanced ATT needs LE credit-based channels (3.14), and
  with the default of 3 it fails at "Failed to create GATT database" and drops the adapter. The
  adapter is "Ozone TV", Audio/Video set-top box, powered on when it appears (`AutoEnable`).
- **Sound** is BlueALSA 4.0 (`bluealsa -p a2dp-source -p a2dp-sink`, SBC; no HFP, so no headset
  microphones). While headphones or a speaker are connected, `ozone-bluetooth agent` writes their
  address and rate to `/run/ozone-bluetooth-audio`, and `sound-config` puts BlueALSA's PCM where
  `hw:0,N` was: the softvol Master, the equalizer and the panel's volume stay as they are. Without
  channels and rate pinned on that last plug, softvol over BlueALSA's ioplug found no configuration,
  as with the equalizer. amplay syncs to `snd_pcm_delay`, which counts BlueALSA's buffer and
  encoder; the headphones' own delay isn't reported. No AC-3 / DTS passthrough then. Programs
  already playing keep their old device until they open it again; when headphones go away under
  amplay, it opens `default` again (on the TV by then), after `snd_config_update_free_global()`:
  alsa-lib rereads only a changed `alsa.conf`, not the `asound.conf` it loads. A disconnected
  BlueALSA PCM has no room and `snd_pcm_wait` fails at once with ENODEV; amplay looped on that
  wait with a whole core before. From a phone, `bluealsa-aplay` plays to the default device and
  lets it go as soon as the stream stops.
- **Input**: HIDP and UHID create event devices like USB ones: kbd-merge takes keyboards,
  `/dev/input/mice` mice. Without udev nodes made after boot were root's only, so `ozone-hw` sets
  `/proc/sys/kernel/hotplug` to `lib/ozone/hotplug`, which gives them the input group (gamepads
  for RetroArch, USB ones too). The box has no uevents at rest, so it costs nothing.
- **`ozone-bluetooth`** (rofi menu; the user is in the `bluetooth` group for BlueZ's D-Bus policy,
  and `audio` for BlueALSA's): the paired devices (connect, disconnect, forget), **Add a device**
  (discovery, with found devices added to the open rofi list as they come, `-async-pre-read 0`;
  all with a name, LE ones too, such as the side of headphones for their app, "JBL ...-LE") and
  **Let a phone find the box** (discoverable for 3 minutes).
  `ozone-bluetooth agent` (bspwmrc) is BlueZ's default agent, KeyboardDisplay: codes to type
  (`rofi -e`), yes/no questions in rofi, one at a time (a new one, or BlueZ's Cancel, closes the
  last). It closes the screensaver first and resets X's idle time: the screensaver's grab kept rofi
  from the keyboard, and rofi gave up, which refused the pairing. It also keeps the panel's
  `bluetooth` module (`~/.cache/ozone-bluetooth.panel`, a polybar hook): the icon alone, as the
  panel is full and what doesn't fit goes off its right end (a device's name there took the close
  and power buttons with it).

Tested here with the laptop (BlueZ, PipeWire) both ways: the box paired from the menu (no question,
as with headphones) and by the laptop (the code confirmed on the TV), the box's sound in the
laptop's speakers (a tone through `default`, 48 and 44.1 kHz, the equalizer on; a TV recording in
amplay), the laptop's sound on the TV, and back to HDMI when it disconnects. Not tried: real
headphones, keyboards, mice, gamepads.

## Android files

Two things in native Linux are Android (bionic) programs: `remotecfg`, which teaches the kernel's
IR decoder the remote's keys (`ozone-input`), and `dtvfe`, which runs the TV app's frontend
libraries. `android-files.txt` lists what they use, found with strace: Android's linker,
`remotecfg`, `build.prop` (for System info) and 60 libraries, 29 MB, as the TV libraries pull in
most of Android's native ones through `libamplayer.so`.

`./install-android-files.sh <ip> [system.img]` (run by `install-desktop.sh`) takes them out of the
backup of the stock system partition with debugfs, no root needed, into
`/usr/local/lib/ozone/android` on the box, and `/etc/init.d/ozone-android` bind-mounts that on
`/system` at boot, where Android's linker looks. Native Linux then reads nothing of Android's
partitions but `data`, which holds `debian.img`. If the copy is missing, `ozone-android` mounts the
NAND partition as before.

## Television (DVB-T/T2)

`./install-tv.sh <ip>` (needs the Android NDK here, `~/Android/Sdk/ndk/*`, and amplay from
`install-desktop.sh`). Super+T or the TV button on the panel: channel list, "Scan channels" the
first time. What the TV does for the viewer is in [MANUAL.md](MANUAL.md#6-television-dvb-t2).

```sh
ozone-tv scan                    # UHF 21..60 (474-786 MHz, 8 MHz), prints level and services per multiplex
ozone-tv scan --freq 570000      # one multiplex (bandwidth and PLP may follow)
ozone-tv scan --nit 570000       # the multiplexes the network table lists, from that one
ozone-tv status 570000           # signal level/quality once a second: for aiming the antenna
ozone-tv antenna on              # 5 V on the antenna input (kept in the settings)
ozone-tv play 3                  # channel 3 of the list
ozone-tv log                     # what the frontend libraries say
ozone-tv --test mux.ts scan      # without an antenna: a TS file fed into the demux from memory
dtv/testmux.py mux.ts 150        # (here) such a file: 2 TV services, radio, DVB subtitles, teletext, EIT
```

How it works. The kernel has the DVB demux but no frontend driver: on Android the TV app
(`th.dtv`) drives the Availink **AVL6762** demodulator and MaxLinear **MxL608** tuner itself, from
its libraries (`libth_dtv_external_c.so` and friends), over `/dev/i2c-2`, demodulator reset on GPIO 74,
TS into demux0 through the parallel `ts0` input. Those libraries, firmware patch included, are
reused as they are:

- `dtv/dtvfe.c` is an Android (bionic) program, built with the NDK for Android 4.4's linker (SysV
  hash, no packed relocations). It `dlopen`s the libraries from `/system/lib` and calls
  `External_Init`, `PI_System_Start`, `PI_Tuner_Init`, then `PI_Tuner_DVB_T_StartLock` /
  `CheckSignalStatus` / `SetAntennaPower` (interfaces taken from the disassembly), answering
  `tune`/`status`/`antenna` on stdin.
- `/etc/init.d/ozone-android` puts the Android files on `/system`, read-only, and makes an empty
  KitKat property area at `/dev/__properties__` (bionic crashes without one). The files are copies
  in Debian, not the NAND partition: see "Android files" above.
- `/etc/init.d/ozone-dtv` gives it the rest of what Android's init would: the kernel logger nodes
  in `/dev/log`, and the Android names of the DVB nodes (`/dev/dvb0.demux0`...).
- `ozone-tv` reads PAT/SDT/NIT/PMT through demux section filters (names usually ISO 8859-5, LCN from
  NIT, subtitles and teletext from the PMT, which is kept as broadcast), then sets TS-tap PES
  filters on the PIDs of the services in use and reads `dvr0`.

One `ozone-tv` process drives the tuner: the window, the player, recordings and timers are all
it, and `ozone-tv`, `ozone-tv play N` or a timer started again hand their request to it through a
socket (`~/.local/share/ozone-tv/control`). It keeps `dtvfe` open (1-4 s to start) until it ends,
when nothing is shown, watched or recorded.

**Watching.** The watched service's packets go into the *live buffer*: segment files of 8 MB
(`/dev/shm/ozone-tv-live-*`, the oldest deleted beyond 64 MB, or on the recording drive), which
`amplay --tv` reads at an offset and waits at the end of for more. amplay stays up while channels
change, driven by commands on stdin (`play`, `stop`, `title`, `info`, `signal`, `pin`...; see the top of
`amplay.c`) and telling ozone-tv what the viewer does on stdout (`channel N`, `audio`, `record`...).
A switch is: the new service's PES filters, a tune only for another multiplex (~1.2 s), and in
amplay a new demuxer that reads the service's PAT and PMT (made by ozone-tv) before the data, so
FFmpeg knows the streams at once instead of probing 2 MB of broadcast (3 s); video starts at a
keyframe, the first frame shows as soon as it is decoded (`show_first_frame_nosync`, as the stock
app does) and the clock starts once 0.3 s of audio is queued. Measured from the key: the picture
in 1.2-2.0 s on the same multiplex, 2.6-3.2 s on another (it was 3.2-5.4 and 4.3-6.5 s with a new
amplay each time). The rest is the broadcast's keyframe interval: 1 s on most of Zeonbud's
channels, about 2 s on СТБ.

The demux's DMA (`/sys/class/stb/asyncfifo0_flush_size`) passes data to `dvr0` in blocks of
256 KB after boot: at a service's 2-5 Mbit/s that held a new channel's first packets back by
0.5-1.3 s. `ozone-tv` sets 16 KB.

The same files are the timeshift buffer: pausing keeps the data coming; amplay keeps a keyframe
index (timestamp, offset) and restarts its demuxer at the keyframe before the time wanted, which
shows a picture at once. A jump in the timestamps (an encoder restarting, a test file looping)
restarts the session there, as a switch would.

**Tracks.** Audio is decoded in software (FFmpeg), so a track change is the decoder following
another PID, and left / right is a copy of one side to both in amplay. DVB subtitles are decoded
by FFmpeg's `dvbsub` and drawn with X onto the player window (pixels with alpha below 128 left in the
color key, so the video shows through); teletext goes through libzvbi (`ttx.c`), its pages
rendered to RGBA the same way, teletext subtitle pages too. The aspect key sets
`/sys/class/video/screen_mode`.

**Recording** is the service's packets written to a `.ts` (PAT and PMT first) with a `.json` next
to it, on the first USB drive, the chosen one, or `/mnt/nand/linux/Recordings`; while the watched
service and the recorded one differ, `ozone-tv` splits the packets by PID. Recordings play in amplay
as files (`--program <sid>`). **Timers** are in `timers.json`; `ozone-tv timers` (from bspwmrc)
starts the TV process 90 s before one is due if none runs, and that process starts and stops the
recordings. Two recordings on different multiplexes conflict (one tuner).

**Parental control**: locked channels, and EIT parental ratings (descriptor 0x55) at or above the
age set, make the player ask for the PIN (amplay's prompt, digits; sha256 in `settings.json`, 0000
until set).

The TV window (`ozone-tv`, GTK) has the lists (all, favourites, radio, recordings, timers), the
selected channel's guide, a menu per channel (info and PIDs, lists, rename, move, skip, lock,
delete, sort) and the settings. The guide is the broadcast EIT, read by one section filter on PID
0x12 (tables 0x4e-0x6f) whenever something is tuned: Zeonbud's multiplexes carry now/next and a
week of schedule for their own services and for the others', so watching or scanning fills in most
of it. `ozone-tv guide` spends 25 s on each multiplex for the rest. It is kept in
`~/.local/share/ozone-tv/epg.json`, and the TDT (PID 0x14) corrects the clock if the box's is off.
Channels are numbered by their place in the list, as the stock app does: the NIT gives the same
LCNs to services of different multiplexes. A rescan keeps the viewer's order, names and flags.

Not done: descrambling (channels found encrypted get a lock).

Logs, for debugging without a network: every `ozone-tv` run writes `/mnt/nand/linux/logs/tv-<time>-<command>/tv.log`
(what it did, the frontend's replies, the libraries' Android log, the kernel log, amplay with the
time of each step of a switch, the signal every 3 s), and a scan adds 3 s of each multiplex found
(`mux-<MHz>.ts`, replay with `--test`).
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
restore. One instance: opening it again adds a tab, and closing the window quits it. It used to
start hidden with the desktop (`--background`, ~60 MB at first and more after use), which showed
it in ~0.1 s; that is off now, for the memory, and it takes ~4 s to start.
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
  search to `yt`; elsewhere the play button (F7) gives `yt` a source of the page's `<video>` (a
  media file goes straight to amplay: ~2 s), an embedded player (`<iframe>` of YouTube, Vimeo,
  Dailymotion, any `/embed/`), or the page itself for yt-dlp (~10-15 s on this CPU), with
  ozone-web's cookies for sites that need signing in (`cookies.sqlite` to a temporary cookies.txt,
  mode 600, removed after; never for YouTube, where yt-dlp signed in changes client);
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

The browser renders on it with our own WebKit (`./install-webkit.sh <ip>`, built by
[webkit/build.sh](webkit/build.sh)), which hands its frames to X as pixmaps: then by default, and
`OZONE_WEB_GPU=0 ozone-web` goes back to the CPU. Wheel scrolling Wikipedia runs at 25 frames a
second instead of 12, CSS animations several times faster; a static page scrolls a little slower
(29 against 32), and it takes ~50 MB more memory (GPU buffers). Without our WebKit, `OZONE_WEB_GPU=1` uses
Debian's through the shims in [gpu/egl-fix](gpu/egl-fix), slower (21 frames a second scrolling).
See [gpu/README.md](gpu/README.md#a-webkit-of-our-own-in-progress).

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
amplay --program 101 recording.ts           # one service of a broadcast TS
amplay --tv                                 # the TV player, run by ozone-tv (see Television)
amplay --control URL                        # driven by another program (DLNA): stdin/stdout
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
- YouTube sends one long request at about twice the video's bit rate (~1 MB/s for 1080p60, on a
  cable that does 9.5), and a request for a 10 MB range at the full line rate. amplay reads HTTP
  files that way, as yt-dlp does: 78 MB in 45 s of 1080p60 instead of 49.
- HEVC needs `dec_format` 15 (`VIDEO_DEC_FORMAT_HEVC`) in SYSINFO, or most frames come late and are
  dropped. Measured with x265 test files: 1080p25 with B-frames and 720p play at 24-25 fps, as
  H.264 does; MPEG-2 576i too. Every codec drops frames in the first 2-3 s (the kernel's start).

Files (from `ozone-files`, or `amplay FILE`): `--report` prints where it stopped, the audio track,
the subtitles and the repeat at the end, and `--start`, `--aid`, `--sid`, `--repeat` take them back.
Audio tracks are the video's program's; a new one restarts the session at the same place (its
decoder or passthrough between the two). Subtitles: every subtitle stream of the file is decoded
in the demuxer's thread (it reads tens of seconds ahead, so a stream chosen later still has its
lines) into one queue, and the chosen one is shown: pictures (DVB, PGS, DVD) as RGBA on the player
window as for TV, text (SubRip, ASS without its styling, mov_text) by osd.c on dark boxes. Files
next to the video (`NAME.srt`, `NAME.LANG.srt`, `.ass`, `.ssa`, `.vtt`) are read whole at the start
with FFmpeg, as Windows-1251 if they aren't UTF-8. PgUp / PgDn jump between chapters where a file has
them.

**Frame rate** (`MATCH_RATE=1` in `/etc/ozone/display`): amplay switches the output to the 50 Hz
mode of the current one for 25 and 50 fps (TV too), back to the 60 Hz one for 30 and 60, through
`/usr/local/lib/ozone/set-mode` (the mode and the desktop scaler; `display/mode` and the scaler
files are the video group's), before the decoder opens and back after it closes. The mode to go
back to is kept in `/tmp/amplay-mode-UID`, so `amplay --reset` restores it after a crash. The
kernel's own automation (`policy_fr_auto`) stays off: it oopses when a decoder closes.

**Passthrough** (`PASSTHROUGH=on`, or `auto` and the EDID's `aud_cap` lists AC-3 / DTS): the
packets go through FFmpeg's `spdif` muxer (IEC 61937 bursts in 16-bit stereo at the stream's rate)
straight to `hw:0,1`, with `audiodsp/digital_raw` 2 and `digital_codec` set before it opens.
softvol and the equalizer are bypassed (they would change the bits), so the volume is the
receiver's. Not tried with a receiver: the HDMI transmitter still logs `Audio Type: PCM`, so the
receiver has to detect the bursts itself, which most do.

## Display and sound

`ozone-display` (a rofi menu, "Display and sound") keeps its settings in `/etc/ozone/display`
(`MODE`, `MATCH_RATE`, `SOUND`, `PASSTHROUGH`, `EQ`) and applies them with:

- `/usr/local/lib/ozone/set-mode`: the output mode and the 1280x720 desktop scaled into it, with
  ozone-screen's margins (kept in 1080p pixels) scaled to the mode. ozone-hw runs it at boot
  (`--boot`: auto keeps u-boot's choice), hdmi-watch when a TV turns up on HDMI while the output is
  analog (`--hdmi`: not if analog was chosen), amplay to match frame rates. A new mode has to be
  confirmed within 15 s (`timeout rofi`), else the old one comes back.
- `/usr/local/lib/ozone/sound-config`: writes `/etc/asound.conf` for the output (HDMI is device 1,
  SPDIF; the analog jack is device 0, I2S; auto takes the analog jack with a `cvbs` mode) and the
  equalizer. Tested: 576cvbs sets the scaler to 720x576 and the sound to device 0, and X carries
  on; nothing was connected to the composite output to look at.
- The equalizer is alsaequal (`libasound2-plugin-equal`, the caps LADSPA `Eq10` filter), its bands
  in `/etc/ozone/alsaequal.bin` (audio group), which `ozone-equalizer` sets with `amixer -D equal`
  and which play at once. In the chain softvol → plug → equal → plug → hw every plug has its format,
  channels and rate pinned: left to negotiate, softvol and the float plugin found no configuration
  ("snd1_pcm_hw_param_get_min: Assertion failed"), whatever the order. ~6 % CPU, whole 16-frame
  blocks still reach the I2S driver (no "unligned").

## Files, music, photos, screensaver

`ozone-files` (GTK): places (the home folders, TV recordings, every mount in `/media`) and a
folder's files. Videos go to `amplay --report` (mpv if it exits 3) with what was saved in
`~/.local/share/ozone-files/state.json`; music to one `/usr/bin/mpv --no-video --ao=sdl` with a JSON
IPC socket, the folder's songs as its playlist. The mpv has PR_SET_PDEATHSIG to SIGKILL: SIGTERM made it
hang on a lock while quitting (SDL's audio thread). Photos go to `ozone-photos`.

`ozone-photos` (GTK, cairo through python3-gi-cairo; WebP through webp-pixbuf-loader; also the
default for images, `~/.config/mimeapps.list`) draws in a DrawingArea:
- A photo comes in a thread scaled to the screen while it is decoded (0.36 s for a 4000x3000 JPEG),
  with its EXIF orientation; its neighbours are loaded next. Zooming in decodes it in full (at most
  3000 pixels, 1.6 s) and draws from that. A GIF that moves plays from its PixbufAnimation, its
  frames scaled with `FILTER_FAST`. GdkPixbuf's `new_from_file_at_scale` fails on those
  ("Not all frames of the GIF image were loaded"): their stills come from the animation.
- Every picture is copied into the X server once (`create_similar_surface`, 16 bit like the
  screen): painted from client memory, Xorg spent 86 % of a core on a playing GIF, 17 % now; a
  1.1 s fade takes Xorg to about 18 % while it runs, a still photo nothing.
- The thumbnails are a grid drawn by hand (only the rows on the screen), made by two threads, the
  ones on the screen first, kept as JPEGs in `~/.cache/ozone-photos` by path, size and time.
- bspwm mapped the window floating, a small square, though it asks for full screen: bspwmrc has
  `bspc rule -a Ozone-photos state=fullscreen` (the class, not the instance).
- `--screensaver` is the screensaver (`ozone-screensaver` starts it; `ozone-files --screensaver`
  runs it): shuffled photos fading into one another, or a moving clock; the first key, click or
  move ends it. ozone-remote treats its window as Files': exit is Esc, info its own key.

Copy and paste: marked paths (or the one chosen, and a video's subtitle files) go to a clipboard;
pasting runs a `Copier` thread (1 MB chunks, fsync, progress every 0.4 s at the top, a move on the
same drive is a rename, a stopped file is deleted, `copystat` failures on FAT / exFAT / network
folders ignored). Info: `amplay --probe FILE` (ffprobe's JSON from amplay's own libavformat: the box
has no ffprobe) for videos and music, GdkPixbuf and a small EXIF reader (IFD0, the Exif and GPS
sub-IFDs) for photos. ozone-remote passes **exit** to Files as Esc (it closed the window before) and
**info** to it as its own key.

Formatting (`ozone-media format DISK|PATH TYPE [LABEL]`, root; asked in rofi by
`ozone-media format-menu`, which the drives menu and Files' menu run): the whole disk, never the
NAND (only `sd*` and `mmcblk*` whole disks, nothing of them used outside `/media`). It unmounts
the disk's partitions, `wipefs` them, writes one partition with sfdisk (MBR, GPT past 2 TiB; type
c / e for FAT32 / FAT16 under 512 MB, 7 for exFAT and NTFS, 83 for ext4), runs mkfs.vfat,
mkfs.exfat (exfatprogs), mkfs.ntfs -Q or mkfs.ext4 and mounts the result. ext4 goes without
`metadata_csum`, `metadata_csum_seed` and `orphan_file`, newer than kernel 3.10, and its root
is given to the user. Meanwhile `/run/ozone-media/formatting/DISK` (with the pid) keeps the daemon
off the disk until the format has mounted it itself: the partition's uevents came while mkfs ran,
and a daemon mounting at the same time mounted the new drive twice. Tested on loop devices (each
type, a drive in use, the GPT way) and on a USB stick (exFAT and back to FAT32, from the menu too):
1-2 s for 16 GB.

`ozone-screensaver` (started by bspwmrc) reads X's idle time from the MIT-SCREEN-SAVER extension
(libXss through ctypes) every 15 s and starts `ozone-photos --screensaver` after the minutes set,
unless amplay, mpv or RetroArch runs or the box is in standby. The saver grabs the keyboard and
pointer once it is mapped, so the first key ends it wherever the focus was.
The same daemon puts the box to sleep after `~/.config/ozone/sleep.json`'s hours (4 by default, 0
never; the power menu sets them through `ozone-screensaver hours N`) of X idle time, whatever
plays: 60 s before, `ozone-screensaver warn` (bspwm's `above` layer) counts down, and quits at the
first input. The player holds X's keyboard, so the countdown also reads X's idle time each
second (below 3 s: somebody pressed something), and the daemon kills it when idle drops. At 0 it
stops amplay (SIGTERM: Files keeps the position, TV goes back to its window) and the screensaver,
then starts `ozone-standby`. Not while `ozone-system root` works (an update, a backup), and only
in native Linux. A window opening on the
desktop ends it too: bspwm takes full screen away from a fullscreen window when another one is
added, which left the saver tiled beside it.

## Calendar

`ozone-calendar` (GTK): the month grid, the chosen day's events beside it, and an event's form in
the same place (no dialogs: everything is moved through with the arrows, and a hint bar says what
the keys do). Events are kept in `~/.local/share/ozone-calendar/events.json` (date, time or all
day, repeat weekly / monthly / yearly, minutes of reminder before). Holidays are Ukraine's, by
the 2023 laws from 2024 on (Christmas on 25 December, 8 May, 15 July, 1 October), Easter and
Trinity by the Julian reckoning. `ozone-calendar reminders` (bspwmrc) looks at the events every
20 s, keeps what it showed in `reminded.json` (and what was put off by 10 minutes), wakes the box
from standby as TV timers do, and starts `ozone-calendar popup`, a dialog in bspwm's `above` layer
(`bspc rule -a Ozone-reminder`), over fullscreen windows. A GTK entry moved between rows is taken
out of its row first: a container dropped with it inside destroys it, and its text with it.


## Code editor

`ozone-code` runs Geany (1.38, GTK 3; about 3 MB with its file tree plugin; `g++` beside the gcc
amplay needs) in the dark GTK theme and the POSIX session's `LANG` switched to C.UTF-8. The first
run writes `~/.config/geany/geany.conf` from `/usr/local/share/ozone/geany` (Catppuccin Mocha
colours, JetBrains Mono, spaces, the terminal is `ozone-terminal -e`, as Debian's
`x-terminal-emulator` is urxvt) and copies two examples into `~/Code`; every run refreshes the colour
scheme and `geany.css` (Geany's message colours are dark red and blue). Debian has no `python`:
`filedefs/filetypes.python` builds and runs with python3. Files opens text and code in it (`TEXT` in
ozone-files), and `mimeapps.list` makes it the default for text. A small C file builds in 0.3 s.

## Updates, backups, the reset

`ozone-system` (GTK) is the window; `ozone-system root …` (through `sudo -n`) does the work and
prints its progress (`progress FRACTION TEXT`, `log`, `warn`, `restart reboot|desktop`, `error`).
- **Updates**: `apt-get update` and a simulated `upgrade` list the packages; installing runs
  `apt-get upgrade --no-install-recommends` with `--force-confold` and `APT::Status-Fd` for the
  progress, and checks that `/sbin/init` is still sysvinit's before and after. yt-dlp is
  compared with its latest GitHub release and updated with `-U`. The box software: GitHub's
  compare API between `/etc/ozone/commit` (written by install-desktop.sh and by updates) and
  `main`; a commit GitHub hasn't got (installed from a computer before pushing) gets no update.
  The update downloads the commit's tarball from codeload and runs its `native/update.py` as root
  (below). `ozone-system check` (bspwmrc) does all that once a day and writes
  `~/.cache/ozone-system/updates.json`.
- `update.py`: replaces each `rootfs/` file the box has that differs; a new file is installed when
  the part of the system it belongs to is (the install script that names it, by a file only that
  script installs; install-desktop.sh's and prepare-debian.sh's always). The user's files: replaced
  only if they are still a version of ours, by sha256 (`/var/lib/ozone/dotfiles.sha256`, and the
  versions in git history that install-desktop.sh sends); a copy of ours is kept in
  `/usr/local/share/ozone/dotfiles`. It installs the packages the installed parts' install
  scripts name (their `apt-get install` lines) and rebuilds amplay when the hash of its sources
  differs (`/var/lib/ozone/amplay.sha256`). What only a computer builds (kernel modules, btusb,
  dtvfe, wireguard-go, WebKit, wpa_supplicant-p2p) it only reports, when its sources' hash
  changes. install-desktop.sh puts the user's files through it too (`--dotfiles-only`), so it no
  longer overwrites what the user changed.
- **Backups**: the settings with Python's tarfile (`home/` for the hidden entries of the home
  folder but `.cache`, `root/` for the system's settings files), 8.6 MB here in 14 s. The whole
  system: `tar --one-file-system --xattrs --numeric-owner` of `/` (without the mount points' contents
  and `/system`, which is a bind mount of the same filesystem) into `zstd -1 -T4`: 1.43 GB into
  567 MB in 2.5 min; NAND reads limit it (gzip -1, one thread, managed 6.5 MB/s).
- **Restoring the whole system**: a new sparse `debian.img.new` the size of the old one, mke2fs'd
  with the running root's features (`-O none,…` from dumpe2fs: the stock kernel's ext4, no
  `flex_bg` or `metadata_csum`), mounted on a free loop device, the archive extracted into it,
  checked (`/sbin/init`, `/etc/inittab`, the modules, `e2fsck -f -n`), then renamed over
  `debian.img`. The running system keeps the old inode through loop0 until the restart; the
  initramfs then mounts the new file, and the data partition's mount frees the old one. 8 min
  here, written to NAND; it needs free space about the size of the system.
- **Restoring settings and the reset** leave `/var/lib/ozone/pending.json` (with the archive
  copied next to it) and restart; `/etc/init.d/ozone-system` (S01, before WiFi, Bluetooth and the
  drives) runs `ozone-system root pending` once (renamed to `.done` first). The archive's members
  are written only below the home folder or into the list of settings files, and never through a
  symbolic link. The reset deletes the hidden entries of the home folder but `.ssh` (and with
  "everything" the rest of it and the TV recordings), copies `/etc/skel` and our dotfiles back,
  deletes the settings files (the tools fall back to their defaults) and the Bluetooth pairings,
  sets Europe/Kyiv and runs `net-apply proxy` and `sound-config`. Both first back up the settings
  onto the box (`auto`, the last four kept).

## Hints, exit, gamepads

- Every window made for the remote says what its buttons do: a hint bar (Calendar, Updates and
  backups, System info, Files, TV, the equalizer, the browser under the page), rofi's own line
  (`textbox-keys` in `config.rasi`, under every menu), amplay's info panel ("Buttons: …", for files
  and TV), the photo viewer under the first photo and after a key it doesn't use.
- exit (ozone-remote `back()`): Esc for the windows in `ESC_CLASSES` (YouTube now too, whose Esc
  goes back through its views); `KEY_BACK` (XF86Back) for `BACK_CLASSES` (ozone-web, which goes
  back a page, then closes the tab, then the window, as Android's Back); anything else is closed.
- Gamepads (`Gamepads` in ozone-remote): every 3 s it looks for devices with `BTN_SOUTH` (no udev:
  Bluetooth pads come and go), reads them without grabbing, and feeds their buttons to
  `Remote.key` as the remote's (D-pad, hat or stick past 60 % = arrows, A OK, B exit...), so mouse
  mode and the on-screen keyboard work with them too. Nothing while RetroArch runs (`pgrep`, at
  most once a second). Tested with a uinput gamepad.
- Panel messages (`ozone-notify`): at most 32 characters, and the processor, memory and
  temperature modules hide while one shows (polybar's `module_hide`): the panel is full, and a
  long message pushed the clock and the power button off the screen. Only the newest message's
  timer clears it (a stamp in `/tmp/ozone-notify.UID`); before, the first message's timer cut the
  second one short.
- Logs: there is no logrotate; `/etc/init.d/ozone-system` cuts a `/var/log/*.log` over 1 MB to its
  last 256 KB at each start (`miracast.log` had reached 3.3 MB).

## Calculator and maths

- `ozone-calc`: Qalculate! 4.5.1 (Debian's; qalculate-gtk, libqalculate, gnuplot-x11 for its
  plots), dark, the exchange rates updated weekly (`auto_update_exchange_rates=7`, seeded on the
  first run). This version gets some indefinite integrals wrong (`integrate(x^2*sin(x), x)` comes
  out as zero) and crashed on `integrate(ln(x)/(1+x^2), 0, inf)`; it also reads `inf` as inches
  (`infinity` works). The newest (5.13 on GitHub) has no ARM build.
- `ozone-math`: wxMaxima 22.12 on Maxima 5.46 (GCL), with maxima-share (ode2, draw...) and
  maxima-doc. Debian's docker image leaves `/usr/share/doc` out of every package
  (`/etc/dpkg/dpkg.cfg.d/docker`), where Maxima keeps its help index: without it every start
  printed "unable to set up the help system"; `etc/dpkg/dpkg.cfg.d/ozone-maxima` lets those two
  folders in. The first run seeds `~/.config/wxMaxima.conf` (no tips, Enter evaluates,
  `invertBackground` for a dark sheet, zoom 1.2). GCL compiles the draw package on its first load
  (4 min on the box, then 1 s, cached in `~/.maxima/binary`): install-desktop.sh does it once, and
  ozone-math in the background when it is missing. Plots go through gnuplot into the sheet; the
  examples in the manual take 1-2 s each. Both together take about 110 MB of the root
  filesystem.

## Programs

`ozone-apps` (GTK) is a store over apt. Its index is built from apt's own lists
(`/var/lib/apt/lists/*_Packages.lz4`, read with `apt-helper cat-file`: 48 MB in 0.6 s, parsed in
10 s, kept in `~/.cache/ozone-apps/index.pickle` until the lists change, then 0.6 s): name, the
newest version (`apt_pkg.version_compare`, from python3-apt; `apt.Cache` itself took minutes to go
through 61 000 packages on the box), section, sizes, the short description, the homepage and the
debtags, which tell windows (`interface::x11`, `uitoolkit::*`) from terminal commands. The lists
have no long descriptions (no Translation files are fetched). A search leaves out libraries and the
like by section and by name.
- Picked programs (`CATALOGUE`): GTK, Qt or SDL without OpenGL (the X desktop has none), small;
  mupdf (84 MB with its fonts) and the 3D games were left out.
- Before installing, `apt-get -s install --no-install-recommends` as the user lists what comes
  with it; their sizes against the free space (plus the download and 40 MB) decide whether it can
  go. `ozone-apps root install|remove` (sudo -n) runs apt-get with `APT::Status-Fd` for the bar,
  checks `/sbin/init` before and after, and cleans the archives.
- Removing (`--autoremove`) is offered only for what was installed here
  (`~/.local/share/ozone-apps/installed.json`; the root side reads `SUDO_USER`'s copy), and refused
  when it would take any other package that was installed on purpose (not apt's
  `Auto-Installed`): the desktop's own packages were all installed by hand, so they stay.
- Open: the package's `.desktop` file through Gio with `GTK_THEME=Adwaita:dark`, or a command of
  its in `ozone-terminal` (only for terminal programs, picked ones, or ones installed here).
- Tested with Mines: 3.7 MB with what it needs, installed, opened, removed, the space back.

## Interface size, wallpaper, screenshots

- Interface size: `DESKTOP=` in `/etc/ozone/display` (ozone-display: 1280x720, 1120x630, 1024x576,
  960x540). The desktop script sets fb0 to it before X starts (ozone-hw keeps 1280x720 for the
  splash), and set-mode takes the scaler's source from fb0's `virtual_size` (double height), so the
  OSD scaler stretches the smaller desktop over the same screen: everything bigger at once, as
  Android's display size, with no app of ours changed. The Mali X driver runs at 1024x576 as at
  1280x720. polybar's `launch.sh` starts `bar/compact` (no cpu, memory, temperature, eth) below 1280.
- `ozone-wallpaper`: feh `--bg-fill` / `--bg-max` or `xsetroot -solid`, from
  `~/.config/ozone/wallpaper.json` (bspwmrc runs `ozone-wallpaper apply`); a user's photo is copied
  to `~/.local/share/wallpapers/chosen.EXT`. Choosing is `ozone-photos --wallpaper FOLDER` (its
  thumbnails, OK sets); yellow sets one in Photos at any time. Five more pictures in
  `/usr/local/share/ozone/wallpapers`, from `tools/wallpaper.py` with palettes (960x540: they are
  blurred blobs, which scale up unharmed).
- `ozone-screenshot`: fb0 itself (RGB565 through a 65 536-entry table, then GdkPixbuf's PNG): 1.7 s
  for 1280x720. X's copy lags behind GL windows that swap straight to the framebuffer; the video
  layer is in neither.

## Sharing (ozone-share)

`/etc/ozone/share.json` (smb, dlna, write) and `/etc/init.d/ozone-share` (`ozone-share apply`);
Debian's smbd, nmbd, samba-ad-dc and minidlna services are disabled by install-network.sh and run by
it instead.
- Samba: `smb.conf` written whole (standalone, `map to guest = Bad User` with the box's user as the
  guest account, `force user`, SMB2 and newer, `hosts allow` the private ranges), a share per folder
  that exists. A guest could list but create nothing (`check_parent_access_fsp` refused the folder)
  until the user had a passdb entry: `smbpasswd -a -n` (no password; empty passwords are refused at
  logon, checked). `wsdd -4 -u nobody:aid_inet` announces it to Windows 10 and 11, which no longer
  browse NetBIOS; start-stop-daemon tracks it by its pid file, as it is a python script.
- MiniDLNA as the user through `start-stop-daemon --chuid USER:aid_inet` (the paranoid network
  check), `/etc/ozone/minidlna.conf`, its database in `/var/cache/ozone-minidlna`; SSDP's 1900
  shared with ozone-dlna's renderer (both SO_REUSEADDR).
- Tested from the laptop: `smbclient -L` as a guest, reading, writing only when allowed, the DLNA
  server found by M-SEARCH (`Ozone TV`, port 8200).

## L2TP/IPsec and PPTP (ozone-vpn)

- The stock kernel has IPsec (XFRM, ESP, the transport mode, PF_KEY) but not `XFRM_USER`, which
  strongSwan needs (Debian builds no kernel-pfkey plugin; its kernel-libipsec does tunnel mode only,
  not L2TP's transport). `vpn/build-xfrm-user.sh` builds `xfrm_user.ko` from the btusb / GPU tree:
  all 78 symbols it needs are in the stock kernel's, and `ip xfrm state|policy add` of a transport
  SA and policy came back field for field; ozone-vpn loads it on the first L2TP connection.
- L2TP/IPsec: `/etc/ipsec.conf` and `ipsec.secrets` written for the one server (IKEv1, PSK,
  transport, `17/1701`), charon's retransmits cut to give up in ~20 s
  (`/etc/strongswan.d/ozone-vpn.conf`), then xl2tpd (`/run/ozone-vpn/xl2tpd.conf`, `c ozone` on its
  control pipe) and pppd on ppp1. PPTP: `pppd pty "pptp SERVER --nolaunchpppd --nohostroute"`, MPPE
  128 with MS-CHAPv2. Both: a host route to the server through the usual gateway, then 0/1 and
  128/1 on ppp1; DNS by `usepeerdns` and Debian's ip-up.d hook. The servers are JSON in
  `/etc/ozone/vpn/NAME.l2tp|.pptp` (root, 600).
- Tested: the failures (a server that doesn't answer, one that refuses) end in ~20 s / ~5 s with the
  routes and daemons gone; not yet against a real server.

## YouTube from a phone (ozone-cast)

The receiver side of what the YouTube app does with TVs, without Google's Cast:
- DIAL: an SSDP answer to `ST: urn:dial-multiscreen-org:service:dial:1` (port 1900 shared with the
  DLNA renderer, SO_REUSEADDR), a device description with `Application-URL` on port 8008, and
  `/apps/YouTube` (its state and the screen id). A launch POST brings `pairingCode`, which goes to
  `/api/lounge/pairing/register_pairing_code` for this screen.
- The Lounge API, as a `LOUNGE_SCREEN`: a screen id from `generate_screen_id` and a lounge token
  from `get_lounge_token_batch` (two weeks; renewed a day early; `~/.config/ozone/cast.json`), then
  `/bc/bind`: a POST with `count=0` brings the session (`c` = SID, `S` = gsessionid), a long GET
  (`RID=rpc`, `TYPE=xmlhttp`) the commands as `LENGTH\n[[index, [command, args]], ...]` chunks
  (parsed with `raw_decode`, not by the length), and POSTs with `count`, `ofs` and `req0_*` fields
  send `nowPlaying`, `onStateChange` (-1 unstarted, 0 ended, 1 playing, 2 paused, 3 loading),
  `onVolumeChanged`. A TV code is `get_pairing_code`.
- Playing: yt-dlp with yt's format and height, then `amplay --control [--audio] [--start]`; its
  `pos` lines give the state; the next video of the phone's queue follows at the end.
- Tested with the Lounge API's remote side from the laptop (a script as the phone): connected,
  `setPlaylist` played ("Me at the zoo", 12 s to start), pause, a seek and `stopVideo` reached the
  player. Not yet with a real phone.