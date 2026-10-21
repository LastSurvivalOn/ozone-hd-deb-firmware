# To do: what stock Android has and native Linux doesn't yet

Collected 2026-10-21 from the stock apps on `/system` (the TV app `th.dtv`, SettingsEx,
MoviePlayer, DLNA, Miracast, OTAUpgrade, the phone remotes), `build.prop` and the hardware, against
what native Linux does now. Levels:

- **Easy**: a day or less, known ground (a script, a menu, a small change to an existing tool).
- **Medium**: a few days, or new code in amplay / ozone-tv, or something not tried on this kernel.
- **Hard**: open-ended: kernel bugs, vendor blobs, protocols to implement, or no known way yet.

Tick an item off (`[x]`) when it is done and update MANUAL.md with it.

## 1. Television (DVB-T2)

- [ ] **Easy**: audio track choice (language, first/second preferred language, stereo / left /
  right mono). Now amplay takes FFmpeg's "best" audio stream; ozone-tv already parses the PMT
  audio PIDs with their languages.
- [ ] **Easy**: signal level and quality on screen at any time (a key in the player, a column in
  the TV window). Now only during a scan, in "no signal" and in the logs.
- [ ] **Easy**: manual scan of one frequency (UHF channel or MHz, bandwidth, PLP), and NIT search.
  Now only the fixed 40-frequency scan.
- [ ] **Easy**: favourites, in groups.
- [ ] **Easy**: antenna power (5 V for an active antenna) as a setting in the TV window. The
  frontend can switch it (it was turned off in a test), there is no switch for it.
- [ ] **Easy**: backup / restore of the channel list and guide to USB, and "reset TV data".
  Now only by copying `channels.json` by hand.
- [ ] **Easy**: banner timeout setting.
- [ ] **Easy or Medium**: radio. Scans find the radio services, nothing plays them: amplay needs an
  audio-only mode (and a picture of some sort on screen), ozone-tv a radio list.
- [ ] **Easy or Medium**: channel list editing: sort (A–Z, LCN), move, rename, delete, skip.
- [ ] **Medium**: aspect ratio (4:3 / 16:9 / full stretch / letterbox) for the TV picture,
  through the video layer's screen mode in sysfs.
- [ ] **Medium**: parental control: lock channels and the TV menu, age rating from EIT, PIN.
- [ ] **Medium**: channel info: PIDs, codecs, service type, and editing PIDs by hand.
- [ ] **Medium**: recording (PVR) to a USB drive, a list of recordings, PVR settings (length,
  drive). The TS from `dvr0` can be written to a file; amplay hasn't been tried on MPEG-2 or on TS
  files yet. Needs USB mounting first (section 3).
- [ ] **Medium**: timeshift: pause live TV and seek back, on top of recording. Now Space only
  freezes the picture.
- [ ] **Medium**: DVB subtitles with a language choice: FFmpeg has the dvbsub decoder, amplay's
  OSD can draw them.
- [ ] **Medium or Hard**: teletext (with its language): PES decoding and a page renderer.
- [ ] **Medium or Hard**: timers from the guide (record or switch at a time, conflict checks).
  From standby it is a timer in a daemon. From power off it can't be done for now: the RTC wake
  alarm doesn't work (`wakealarm` gives EIO), so only the remote wakes the box.
- [ ] **Medium or Hard**: the TV interface in Ukrainian (the stock app has uk-UA).
- [ ] **Hard / low value**: scrambled channels: BISS keys, CCcam / Newcamd / Gbox clients. Hardly
  relevant to free T2 here.

Not counted: the stock app's satellite features (DiSEqC, motor, LNB), as the tuner is T2 only.

## 2. Video and sound

- [ ] **Easy or Medium**: output resolution and rate (720p / 1080p, 50 / 60 Hz, CVBS PAL / NTSC)
  in a menu. Now the mode comes from u-boot's `hdmimode`, and the 1280x720 desktop is scaled to it.
- [ ] **Medium**: the file player features MoviePlayer has: resume from the last position, audio
  track choice, external (`.srt`) and embedded subtitles, chapters, repeat, file info. Needs USB
  mounting and a file list (section 3).
- [ ] **Medium**: try HEVC (`/dev/amstream_hevc` is wired up in amplay but untested) and MPEG-2 in
  amplay.
- [ ] **Medium**: audio passthrough ("HDMI RAW": AC3 / DTS as a bitstream to an AV receiver).
- [ ] **Medium**: the analog AV output (composite video and stereo): X on `576cvbs` and sound
  through I2S (device 0) are untested.
- [ ] **Medium**: a music player and a photo viewer / slideshow made for the remote (now only plain
  mpv and feh), and a photo slideshow as the screensaver (Android's Daydream).
- [ ] **Medium**: an equalizer (Android's MusicFX), as an ALSA plugin.
- [ ] **Hard**: automatic frame rate (24 / 25 / 50 Hz to match the video, `policy_fr_auto`). It is
  forced to 0 because closing a decoder with it on oopses the kernel.

## 3. Drives and files

- [x] **Easy**: automatic mounting of USB drives and the SD card (there is an `sd` host). There's
  no udev, so it's a hotplug script or a small poller in `ozone-hw`.
- [x] **Easy**: NTFS and exFAT (`ntfs-3g`, `exfat-fuse`; the kernel has fuse and vfat).
- [x] **Easy**: network shares (Samba). Done with rclone for SMB2/3: the kernel's CIFS turned out
  to speak SMB1 only, which it still does for old servers.
- [ ] **Easy or Medium**: a file browser for the remote (files on USB / SD / shares that open in
  the player or the photo viewer), with formatting a drive.

## 4. Network and phones

- [ ] **Easy**: PPPoE, static IP / DNS, proxy, WPS in the WiFi / network menu. Now only choosing a
  WiFi network.
- [ ] **Easy or Medium**: a remote control and keyboard on the phone (Android has RC_Server, the
  Google TV IP remote and RemoteIME), for example as a web page served by the box.
- [ ] **Medium**: a WiFi hotspot (hostapd on the 8189ES; untested whether its driver does AP mode).
- [ ] **Medium**: DLNA: browsing media servers, and a renderer so that a phone can "send" a video to
  the box (for example gmrender-resurrect with amplay).
- [ ] **Medium**: VPN.
- [ ] **Hard**: Miracast (mirroring a phone's screen): WiFi Direct on `p2p0`, which the driver
  already creates, plus RTSP and H.264 into amplay.

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
- [ ] **Hard**: GPU acceleration (Mali-450). Android draws its interface and apps with GLES; X here
  runs on fbdev with no GPU, as the Mali libraries are built for bionic (libhybris might load
  them). It is the main reason the browser is slow, and video in web pages decodes in software.
- [ ] **Hard / not planned**: Android apps (Google Play, APKs) in Linux. Rebooting into Android
  is the way.

## Neither has it

- [ ] **Medium**: HDMI-CEC: control the box with the TV's remote, turn the TV on and off with the
  box. The kernel has a `cec_input` device, while Android runs with CEC off (`hdmitx=cec0`,
  `ro.platform.has.cecmode=false`).
- Bluetooth: no hardware (`hw.has.bluetooth=false`).
- 4K: the S805 outputs 1080p at most.
- Netflix and other DRM: Widevine is off (`media.amplayer.widevineenable=false`).
