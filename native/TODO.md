# To do: what stock Android has and native Linux doesn't yet

Collected 2026-10-21 from the stock apps on `/system` (the TV app `th.dtv`, SettingsEx,
MoviePlayer, DLNA, Miracast, OTAUpgrade, the phone remotes), `build.prop` and the hardware, against
what native Linux does now. Levels:

- **Easy**: a day or less, known ground (a script, a menu, a small change to an existing tool).
- **Medium**: a few days, or new code in amplay / ozone-tv, or something not tried on this kernel.
- **Hard**: open-ended: kernel bugs, vendor blobs, protocols to implement, or no known way yet.

Tick an item off (`[x]`) when it is done and update MANUAL.md with it.

## 1. Television (DVB-T2)

- [x] **Easy**: audio track choice (language, first/second preferred language, stereo / left /
  right mono). Now amplay takes FFmpeg's "best" audio stream; ozone-tv already parses the PMT
  audio PIDs with their languages.
- [x] **Easy**: signal level and quality on screen at any time (a key in the player, a column in
  the TV window). Now only during a scan, in "no signal" and in the logs.
- [x] **Easy**: manual scan of one frequency (UHF channel or MHz, bandwidth, PLP), and NIT search.
  Now only the fixed 40-frequency scan.
- [x] **Easy**: favourites, in groups.
- [x] **Easy**: antenna power (5 V for an active antenna) as a setting in the TV window. The
  frontend can switch it (it was turned off in a test), there is no switch for it.
- [x] **Easy**: backup / restore of the channel list and guide to USB, and "reset TV data".
  Now only by copying `channels.json` by hand.
- [x] **Easy**: banner timeout setting.
- [x] **Easy or Medium**: radio. Scans find the radio services, nothing plays them: amplay needs an
  audio-only mode (and a picture of some sort on screen), ozone-tv a radio list.
- [x] **Easy or Medium**: channel list editing: sort (A–Z, LCN), move, rename, delete, skip.
- [x] **Medium**: aspect ratio (4:3 / 16:9 / full stretch / letterbox) for the TV picture,
  through the video layer's screen mode in sysfs.
- [x] **Medium**: parental control: lock channels and the TV menu, age rating from EIT, PIN.
- [x] **Medium**: channel info: PIDs, codecs, service type, and editing PIDs by hand.
- [x] **Medium**: recording (PVR) to a USB drive, a list of recordings, PVR settings (length,
  drive). The TS from `dvr0` can be written to a file; amplay hasn't been tried on MPEG-2 or on TS
  files yet. Needs USB mounting first (section 3).
- [x] **Medium**: timeshift: pause live TV and seek back, on top of recording. Now Space only
  freezes the picture.
- [x] **Medium**: DVB subtitles with a language choice: FFmpeg has the dvbsub decoder, amplay's
  OSD can draw them.
- [x] **Medium or Hard**: teletext (with its language): PES decoding and a page renderer.
- [x] **Medium or Hard**: timers from the guide (record or switch at a time, conflict checks).
  From standby it is a timer in a daemon. From power off it can't be done for now: the RTC wake
  alarm doesn't work (`wakealarm` gives EIO), so only the remote wakes the box.
- [ ] **Medium or Hard**: the TV interface in Ukrainian (the stock app has uk-UA).
- [ ] **Hard / low value**: scrambled channels: BISS keys, CCcam / Newcamd / Gbox clients. Hardly
  relevant to free T2 here.

Not counted: the stock app's satellite features (DiSEqC, motor, LNB), as the tuner is T2 only.

## 2. Video and sound

- [x] **Easy or Medium**: output resolution and rate (720p / 1080p, 50 / 60 Hz, CVBS PAL / NTSC)
  in a menu. Now the mode comes from u-boot's `hdmimode`, and the 1280x720 desktop is scaled to it.
- [x] **Medium**: the file player features MoviePlayer has: resume from the last position, audio
  track choice, external (`.srt`) and embedded subtitles, chapters, repeat, file info. Needs USB
  mounting and a file list (section 3).
- [x] **Medium**: try HEVC (`/dev/amstream_hevc` is wired up in amplay but untested) and MPEG-2 in
  amplay.
- [x] **Medium**: audio passthrough ("HDMI RAW": AC3 / DTS as a bitstream to an AV receiver).
  Done in amplay (FFmpeg's spdif muxer to hw:0,1), not tried with a receiver: the HDMI transmitter
  still logs "Audio Type: PCM" with digital_raw and digital_codec set, so a receiver has to detect
  the bursts itself. The TV here takes PCM only.
- [x] **Medium**: the analog AV output (composite video and stereo): X on `576cvbs` and sound
  through I2S (device 0) are untested. Now chosen in Display and sound, the desktop scaled into it
  and the sound sent to device 0; not seen on a composite TV yet.
- [x] **Medium**: a music player and a photo viewer / slideshow made for the remote (now only plain
  mpv and feh), and a photo slideshow as the screensaver (Android's Daydream).
- [x] **Medium**: an equalizer (Android's MusicFX), as an ALSA plugin.
- [x] **Hard**: automatic frame rate (24 / 25 / 50 Hz to match the video, `policy_fr_auto`). It is
  forced to 0 because closing a decoder with it on oopses the kernel.

## 3. Drives and files

- [x] **Easy**: automatic mounting of USB drives and the SD card (there is an `sd` host). There's
  no udev, so it's a hotplug script or a small poller in `ozone-hw`.
- [x] **Easy**: NTFS and exFAT (`ntfs-3g`, `exfat-fuse`; the kernel has fuse and vfat).
- [x] **Easy**: network shares (Samba). Done with rclone for SMB2/3: the kernel's CIFS turned out
  to speak SMB1 only, which it still does for old servers.
- [x] **Easy or Medium**: a file browser for the remote (files on USB / SD / shares that open in
  the player or the photo viewer), with formatting a drive. Files (ozone-files) does it all;
  formatting (FAT32, exFAT, NTFS, ext4) is in its menu and the drives menu, unreadable drives too.

## 4. Network and phones

- [x] **Easy**: PPPoE, static IP / DNS, proxy, WPS in the WiFi / network menu: the Network menu
  (`ozone-network`, the panel's address; `install-network.sh`).
- [x] **Easy or Medium**: a remote control and keyboard on the phone: a web page that ozone-remote
  serves on port 8080 (buttons, a touchpad, typing), with a QR code in the Network menu.
- [x] **Medium**: a WiFi hotspot (`ozone-hotspot`): hostapd on `p2p0`, so the box stays on its own
  WiFi network, sharing the cable, PPPoE or WiFi.
- [x] **Medium**: DLNA (`ozone-dlna`): a renderer that phones and computers send videos, music and
  photos to (amplay, mpv), and the media servers on the network as a place in Files.
- [x] **Medium**: VPN (`ozone-vpn`): OpenVPN and WireGuard (wireguard-go, in user space) files.
- [x] **Hard**: Miracast (mirroring a phone's screen, `ozone-miracast`): WiFi Direct on `p2p0`
  with a patched wpa_supplicant (`install-miracast.sh`), RTSP, and the stream into `amplay --tv`.

## 5. Apps and system

- [ ] **Easy**: date, time and time zone settings in a menu (now set in files: Europe/Kyiv, chrony).
- [ ] **Easy or Medium**: small apps for the remote: clock / alarm, calculator, calendar.
- [ ] **Medium**: YouTube features SmartTube has and `yt` doesn't: account sign-in, subscriptions,
  history, the home feed, channel pages, playlists, SponsorBlock, YouTube's own subtitles, speed,
  chapters. Starting a video also takes ~10 s.
- [ ] **Medium**: settings in the interface: sound, screen, network, language in one place.
- [ ] **Medium**: the interface in Ukrainian (Android's is uk-UA; ours is English).
- [ ] **Medium**: backup / restore of the system and settings, and a "factory reset", from the box
  itself. Now only the laptop scripts.
- [ ] **Medium**: updates from the box (apt and our scripts), in place of Android's OTA.
- [ ] **Medium**: a TV-style home screen with tiles (Android's DTVLauncher); now rofi and the panel.
- [x] **Hard**: GPU acceleration (Mali-450) for X: OpenGL ES 2.0 for games, mpv and code of
  one's own (`install-gpu.sh`, `install-games.sh`, [gpu/README.md](gpu/README.md)).
- [x] **Hard**: the browser on the GPU (`OZONE_WEB_GPU=1`): WebKit composites on the Mali-450
  through shims under it (native/gpu/egl-fix), CSS animations 8 times faster.
- [x] **Hard**: the browser's frames straight to the screen. A WebKit of our own
  (`install-webkit.sh`) hands frames to X as pixmaps, and X draws only what changed: wheel
  scrolling Wikipedia 25 frames a second instead of 12, a small animation with X at 29 % instead
  of 100 ([gpu/README.md](gpu/README.md#a-webkit-of-our-own-in-progress)).
- [x] **Medium**: video in web pages, on the hardware decoder: the play button (F7) hands the
  page's video to `yt` (a `<video>` source, an embedded player, or yt-dlp on the page). WebKit's
  own media stays off: no GStreamer H.264 decoder here, and software decoding can't keep up.
- [x] **Medium**: videos of sites that need signing in (Vimeo, social networks): `yt` gives yt-dlp
  ozone-web's cookies (not for YouTube).
- [ ] **Hard / not planned**: Android apps (Google Play, APKs) in Linux. Rebooting into Android
  is the way.

## Neither has it

- [ ] **Medium**: HDMI-CEC: control the box with the TV's remote, turn the TV on and off with the
  box. The kernel has a `cec_input` device, while Android runs with CEC off (`hdmitx=cec0`,
  `ro.platform.has.cecmode=false`). **Waiting for a TV with CEC**: the box's display now is a
  Samsung S22F350, a PC monitor with none (2026-10-28: it answers no poll, "tv don't have CEC
  feature"). What was found then, in the stock kernel's driver (`drivers/amlogic/hdmi/hdmi_tx/
  hdmi_tx_cec.c`, `hdmi_cec_key.c` in Hardkernel's tree; the stock kernel has the same strings):
  - `/sys/class/amhdmitx/amhdmitx0/cec_config` (hex, root) turns it on at run time and registers
    the box (logical address 4, playback device; physical address from the EDID): bit 0 on,
    1 one-touch play, 2 one-touch standby (the TV's Standby comes as KEY_POWER on `cec_input`),
    3 the TV switched on. Tried: `b` works, `0` turns it off, nothing broke.
  - `/sys/class/amhdmitx/amhdmitx0/cec` takes user commands in hex: `d 0` the TV to standby,
    `e 0` Image View On, `14` Active Source, `15 0` Inactive Source, `2 0` the TV's power status,
    `1a 4` poll the TV. No raw messages.
  - **Every write must end with a space and no newline**: both parsers skip with
    `while (buf[i] != ' ') i++` and no bound, so `echo b` would read past the buffer.
  - Without bit 3 the registration asks the TV's power status and waits for any message with no
    timeout, in the writer, or in the HDMI hotplug thread (it runs on every hotplug): with a display
    that never answers, hotplug handling would stop for good. Keep bit 3 on.
  - Sleep (Android early suspend, ozone-standby) sends Inactive Source and stops CEC; waking sends
    Image View On and Active Source. The TV off with the box is our `d 0`, from ozone-standby.
  - The TV's keys come with codes X can't see (OK is KEY_SELECT, the colours KEY_RED..BLUE): take
    `cec_input` in ozone-remote and map them to remote.conf's. The kernel maps no key for CEC
    0x30-0x37 (channel up/down, info, input select): those can't be had.
- [x] **Medium**: Bluetooth. The box has none (`hw.has.bluetooth=false`) and Android no driver for
  a USB adapter; native Linux takes one now: headphones and speakers, keyboards, mice, gamepads, a
  phone's music on the TV (`install-bluetooth.sh`).
- 4K: the S805 outputs 1080p at most.
- Netflix and other DRM: Widevine is off (`media.amplayer.widevineenable=false`).
