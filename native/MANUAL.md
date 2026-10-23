# OzoneHD box: user manual

How to use the box in native Linux: the desktop, YouTube, TV, the browser, the video player,
sleep and Android. The box's own remote runs everything (section 10); a USB keyboard and a
mouse work too.

**Super** is the key with the Windows logo. "Super+Q" means hold Super and press Q.

## Contents

1. [Turning on, sleep, power off, Android](#1-turning-on-sleep-power-off-android)
2. [The desktop and the panel](#2-the-desktop-and-the-panel)
3. [Windows and workspaces](#3-windows-and-workspaces)
4. [YouTube](#4-youtube)
5. [The video player](#5-the-video-player)
6. [Television (DVB-T2)](#6-television-dvb-t2)
7. [The browser](#7-the-browser)
8. [Picture and sound settings](#8-picture-and-sound-settings)
9. [When something goes wrong](#9-when-something-goes-wrong)
10. [The remote control](#10-the-remote-control)
11. [USB drives, SD cards and network folders](#11-usb-drives-sd-cards-and-network-folders)
12. [Files: videos, music and photos](#12-files-videos-music-and-photos)
13. [Games](#13-games)
14. [The network, the hotspot, VPN, phones](#14-the-network-the-hotspot-vpn-phones)
15. [All key combinations](#15-all-key-combinations)

---

## 1. Turning on, sleep, power off, Android

**Turning on.** Plug the box in, or, after **Power off**, press **Power** on the remote. The pulsing
Ozone HD logo shows while Linux starts, until the desktop appears.

**The power menu** is **Super+Shift+E** or the ⏻ button at the right end of the panel. Choose an
item with the arrows and Enter, or click it. Esc closes the menu.

| Item | What it does |
|---|---|
| Sleep | Turns the screen off. Press any key or click a mouse button to wake it (or any button on the remote; its Power button also puts the box to sleep). Waking is instant. The network keeps working while it sleeps. Quit the video player first: it doesn't stop by itself. |
| Android | Closes Linux and starts the original Android (the stock TV app, SmartTube). |
| Restart desktop | Restarts the graphical session. Open windows close. Use it when something hangs (see section 9). |
| Screen size | Fits the picture to a screen that cuts off its edges, see below. |
| Reboot | Restarts the box into Linux. |
| Power off | Stops the system and turns off the screen; the light on the box turns red. To turn it on again, press Power on the remote (or unplug the box and plug it back in): it starts from the beginning, with the logo, as after plugging in. |

**Screen size.** Some TVs and monitors cut off the edges of the picture: the panel at the top or
the bottom of the player is half hidden. Open ⏻ → **Screen size** (or "Screen size" in the app
menu). A purple frame with arrows in the corners appears:

| Keys | Action |
|---|---|
| **←** / **→** | narrower / wider |
| **↑** / **↓** | taller / shorter |
| **+** / **−** | all edges at once |
| **R** | back to the full size |
| **Enter** | save |
| **Esc** | cancel |

The buttons on the screen do the same with the mouse (or the remote's mouse mode).
Make the frame and the corner arrows just visible at the edges, then press Enter. The setting is
kept across reboots, and videos fit into the same area. For another screen, adjust it again.

**From Android back to Linux:** restart the box from Android, or unplug it and plug it back in.
Linux always starts by default.

## 2. The desktop and the panel

**WiFi.** Click **WiFi** on the panel (or "WiFi" in the app menu; the panel's item opens the
Network menu, section 14, where **WiFi networks** is the second line). After a few seconds a list of
networks appears, strongest first:
- the bars show the signal;
- 🔒 means a password is needed;
- **✓ connected** is the current network, and **saved** ones you've connected to before.

Choose a network and type its password (once: it's remembered), then Enter. The panel shows
"Connecting to …", then the network name and the box's address. Saved networks connect again by
themselves after a reboot. The list also has **Rescan**, **Disconnect** and **Forget a network**.
When the network cable is plugged in and working, WiFi turns itself off after a few seconds. When
you pull the cable out, WiFi connects again within a few seconds. If you turned WiFi off with
**Disconnect**, it stays off. To use WiFi with the cable plugged in, choose the network again in
the list. A router with a **WPS** button: choose **WPS: the router's button** and press the
button on the router within two minutes, or **WPS: a PIN** and type the PIN shown into the router's
page.


The panel is the bar along the top of the screen. From left to right:

| Element | Click |
|---|---|
| ▦ (grid) | app menu |
| YouTube button | YouTube search (section 4) |
| Globe | the browser (section 7) |
| Screen icon | television (section 6) |
| 1 2 3 4 5 | workspaces (section 3). The current one is highlighted; the ones with windows are coloured. |
| yellow text | short messages: "Loading video…", "Scan finished", errors. They disappear after 6 s. |
| centre | the title of the active window. Click it for a list of the open windows, to switch to one. |
| pointer and keyboard icons | the remote's mouse mode and the on-screen keyboard (section 10): green when on, a click switches them |
| drive icon | USB drives, SD cards and network folders (section 11): how many are open; a click gives the list |
| CPU %, memory, temperature | system load |
| IP address | the cable connection (shown only with the cable plugged in). Click it for the Network menu (section 14). |
| WiFi | the WiFi network the box is on. Click it for the Network menu. It stays grey when not connected. |
| 🔈 percentage | volume. Click to mute, scroll the wheel over it to change the volume. |
| date and time | |
| ✕ | closes the active window |
| ⏻ | power menu |

A window in full screen (the video player, TV, a full-screen page) covers the panel. To see it
anyway, move the mouse pointer to the very top of the screen: the panel appears over the window
and stays while the pointer is on it. **info** on the remote shows it for a few seconds (press it
again to hide it).

**The app menu** opens with **Super+D** or the grid button. Start typing a name ("term", "files"…)
and the list filters as you type. Use the arrows to choose and Enter to launch. Esc closes it.

**System info** (in the app menu, or a click on the CPU, memory or temperature on the panel)
tells everything about the box: the firmware and the programs with their versions, what starts
at boot and with the desktop (and whether it runs now), how the system works, the hardware (the
processor's speed and temperature, the TV it is connected to, the WiFi signal), the free space
and the memory. **↑ / ↓** choose a section, **→** goes into it (then **↑ / ↓ / PgUp / PgDn**
scroll, **←** goes back), **Esc** closes. **Copy all** puts the whole report on the clipboard,
to paste into a message.

**The terminal** (the command line) opens with **Super+Enter**, or "Terminal" in the app menu.

| Keys / mouse | Action |
|---|---|
| **Ctrl+Shift+C** / **Ctrl+Shift+V** | copy / paste (or the right mouse button: Copy, Paste) |
| select with the mouse, then the middle button | paste the selection |
| **Ctrl+Shift+T** / **Ctrl+Shift+W** | new tab / close the tab |
| **Ctrl+PgUp** / **Ctrl+PgDn**, **Alt+1…9** | previous / next tab, tab number 1…9 |
| **Ctrl + +** / **Ctrl + −** / **Ctrl+0** | bigger / smaller text / back to normal |
| **Tab** | completes a command, a file name or an option; pressed again, a menu to choose from with the arrows (each option with what it does) |
| **→** or **End** | takes the grey suggestion fish shows as you type (from the commands typed before); **Alt+→** takes one word of it |
| **↑** / **↓** | earlier commands; with some text typed, only the ones that contain it |
| **Ctrl+C** | stops the running command |

With the remote, the on-screen keyboard has **Tab** and **Enter** too.

All menus (apps, power, TV channels) work the same way: type to filter, arrows to choose, Enter
or a click to confirm, Esc to close.

## 3. Windows and workspaces

Windows don't overlap. Each new window takes part of the screen, and the screen is shared out
automatically (a "tiling" window manager). The active window has a lilac border. The mouse
activates whatever window it points at.

| Keys | Action |
|---|---|
| **Super+Q** | close the active window |
| **Super+F** | full screen on/off |
| **Super+S** | make the window "floating" (it can be moved freely), or put it back into the layout |
| **Super+M** | one window over the whole screen (the others sit behind it) / back to the shared layout |
| **Super+H / J / K / L** | move to the window on the left / below / above / on the right |
| **Super+Shift+H / J / K / L** | swap the window with its neighbour in that direction |
| **Super + left mouse drag** | move a window |
| **Super + right mouse drag** | resize a window |

**Workspaces** are five separate "desktops", numbered 1–5 on the panel. For example, the browser
can sit on 2 and the terminal on 3.

| Keys | Action |
|---|---|
| **Super+1 … 5** | go to workspace 1…5 |
| **Super+Shift+1 … 5** | move the active window to workspace 1…5 |
| **Super+Tab** | back to the previous workspace |

## 4. YouTube

Open it with **Super+Y** or the YouTube button on the panel. YouTube links opened in the browser
also come here.

It opens on **Recommended**: videos like the ones you watched last (before you have watched any,
like your last searches). Next to it are **History**, everything watched, newest first, and
**Playlists**. All of it is kept on the box: no Google account, and nothing is sent to YouTube but
the requests for the videos themselves. Click a tab, or press **yellow** on the remote to go to the
next one.

**Playlists.** **Green** on the remote (or **Insert**) on a card offers your playlists: choose one,
or **New playlist…** and type a name (the remote's **text** button gives the on-screen keyboard).
**Watch later** is always there. In Playlists, a card opens its playlist; **Esc** goes back.
**Blue** (or **Delete**) removes the chosen video from History or from the playlist, and an empty
playlist from Playlists. A YouTube playlist (a card in the results, a link pasted into the search
box, or one opened in the browser) shows its first hundred videos, with a **Save as a playlist**
button above them; **Esc** goes back to the results.

**Searching.** Type a query and press Enter. Results arrive in about a second, as cards showing:
- the thumbnail, with the length in the corner (a red **LIVE** marks a live broadcast; a
  playlist, which turns up among the videos as on YouTube, shows how many videos it has);
- the channel's avatar and name;
- the title;
- the number of views and how long ago it was published.

You can start typing anywhere in the window, not only in the search box: the text goes straight
into it. Going to the search box shows your recent searches: **↓** and **Enter** search one again.
A YouTube link pasted into the search box plays at once.

| Keys / mouse | Action |
|---|---|
| typing + **Enter** | new search |
| **arrows** | choose a card |
| **Enter** or a click on a card | watch |
| **↓** in the search box | go to the cards |
| **Esc** | from the search box: go to the cards; from search results or a playlist: back; from a tab: close YouTube |
| yellow / green / blue (or **Insert** / **Delete**) | next tab / add to a playlist / remove |
| **Ctrl+L** or **/** | go to the search box |
| scrolling to the end | loads more results |

**Starting a video takes about 10 s.** The card shows **LOADING…** meanwhile. Then the video opens
full screen. When the video ends or you quit it, the results come back where you left them.

**Quality.** The two lists at the right of the search box set the **highest** resolution
(1080p / 720p / 480p / 360p) and frame rate (60 / 30 fps) a video may play at. The setting is
remembered. The player's title shows what you actually got, e.g. "… · 720p60".

- If the video keeps stopping on **Loading…**, the connection is too slow. Lower the quality:
  first try **720p** with 60 fps.
- YouTube offers 60 fps videos at 30 fps only up to 480p. So "30 fps" means 480p for them.

If YouTube won't play a video (removed, blocked in your region, private), the panel shows why,
for example "This video is not available".

## 5. The video player

The player is used by YouTube, TV and the videos in **Files** (section 12). Moving the mouse, or a
key, shows the controls:
- at the top: the title and ✕ (close);
- at the bottom: the buttons, the time, the progress bar (with a tick at each chapter) and the volume.

They hide again after 3 s.

| Keys | Remote | Action |
|---|---|---|
| **Space**, **Enter** or **P** | **OK**, **pause** | pause / play |
| **←** / **→** | **<<** | back / forward 10 s |
| **PgDn** / **PgUp** | **page− / page+** | back / forward 1 minute; in a video with chapters, the previous / next chapter |
| **↑** / **↓** | **vol+ / vol−** | volume up / down |
| **M** | **mute** | mute on/off |
| **I** | **info** | what the video is: its tracks, subtitles, chapters, and what plays now |
| **A** | **yellow** | the next audio track (languages, a commentary…) |
| **S** | **green** | subtitles: the next language, then off |
| **Z** | **blue** | picture shape: as it is, full screen, 4:3, 16:9 |
| **R** | **red** | repeat: off, this video, all the videos of the folder |
| **Q**, **Esc** or **Backspace** | **exit**, **stop** | quit (exit first closes the info) |

**Subtitles** come from the video itself (text, or pictures as on DVDs and Blu-rays) or from a
file next to it with the same name: `Film.mkv` takes `Film.srt`, `Film.ukr.srt`, `Film.en.ass`…
Subtitle files in Windows Cyrillic (often the case with Ukrainian ones) show right too.

**Videos in Files continue where they were stopped**, with the audio track, the subtitles and the
repeat chosen last time; a video watched to its end starts from the beginning.

What plays: H.264 and H.265 (HEVC) up to 1080p, MPEG-2 and MPEG-1, in the hardware decoder. Other
video (MPEG-4 ASP / DivX, VP9, AV1…) goes to the software player, which manages about 360p.

| Mouse | Action |
|---|---|
| click on the picture | pause / play |
| click on the progress bar | jump to that point |
| ⏪ ⏯ ⏩ buttons | back 10 s / pause / forward 10 s |
| click on the volume | mute |
| wheel | volume |
| ✕ at the top right (it turns red under the pointer) | quit |

**Loading…** in place of the time means the player is waiting for data from the network. It
continues by itself once a few seconds are buffered. Multimedia keys (play, pause, next…) on a
keyboard that has them do the same as the keys above.

## 6. Television (DVB-T2)

Open it with **Super+T**, the screen icon on the panel, or **epg**, **dvb** or **TV/radio** on the
remote. An antenna must be connected to the box.
The TV window opens:
- **left:** a list of channels, with what's on now and a bar showing how far into it they are, and
  the signal level when that frequency was last tuned. Above it, which list: **All channels**,
  your own lists (**Favourites** to begin with), **Radio**, **Recordings** and **Timers**;
- **right:** the programme guide of the selected channel, by day. **NOW** marks what's on, **16+**
  an age rating, **REC** or **ALARM** a timer set on it.

**The first time**, click **Scan channels** at the top right and choose **The whole band**. The scan
checks 40 frequencies, about 3 minutes in all. The top of the window and the panel show the
progress, for example "Scan 12/40: 570 MHz… (found 6)". "Nothing found" means no signal on any
frequency: check the antenna and its cable (and the antenna power, below). Scan again if you move
the antenna or channels change: your order, names, lists, skips and locks stay.

**One frequency**: if you know the multiplex (a UHF channel number like 27, or MHz like 522), with
its bandwidth and, for DVB-T2, its PLP. **Network search** reads the list of frequencies a network
announces and scans those; Zeonbud announces none, so here it finds only the frequency you give.

**The programme guide** comes from the broadcast itself. It fills in by itself while you watch,
record or scan. **Update guide** visits every multiplex for about 25 s each, a minute or so in
all, to fill in the rest. Some channels broadcast no guide at all ("no programme information").

| Keys in the TV window | Action |
|---|---|
| **↑ / ↓** or a click | choose a channel: its guide appears on the right |
| **Enter** or a double click | watch it |
| **0–9** | the channel with that number |
| **→** | into the guide (↑ / ↓ or a click there shows each programme's full description) |
| **Enter** on a programme | watch, record, record every day or week, or switch to it when it starts |
| **←** | back to the channels |
| **Menu** or **F2** (remote: **blue**), a right click | the channel's menu, see below |
| **Tab** (remote: **green**) | the next list |
| **Esc** (remote: **exit**) | back: from the guide to the channels, from Recordings or Timers to the channels; then close the window |

**The channel's menu**: **Channel info and PIDs** (the frequency, the service, the streams, and the
PIDs, which you can change by hand for a channel that plays wrongly), **Lists** (add it to a list
of your own, or make a new one), **Rename**, **Move up / down**, **Skip when switching** (the
channel stays in the list but next / previous pass it by), **Locked** (it needs the PIN), **Delete**,
and **Sort the list** by number or A–Z.

### Watching

At the top: the channel, what's on now (with its times) and what's next. The bars go after a few
seconds without a key (**Settings → Picture and sound** sets how many). When you stop watching,
the TV window comes back.

Switching channels takes one and a half to two seconds on the same frequency, about three to
another one: the player stays up, and the picture starts at the channel's next full frame (the
broadcast sends one every one or two seconds).

| Keys in the player | Remote | Action |
|---|---|---|
| **Enter** | **OK** | the channel list on the left, with what's on every channel |
| **↑ / ↓**, **PgUp / PgDn** (in the list) | | choose |
| **Enter** or **→** (in the list), or a click | **OK** | switch to it |
| **Esc** or **←** (in the list) | **exit** | close the list |
| **→** / **←**, **PgUp** / **PgDn** | **page+ / page−** | next / previous channel |
| **0–9** | **0–9** | the channel with that number |
| **↑ / ↓**, **M** | **vol+ / vol−**, **mute** | volume, mute |
| **Space** | **pause** | pause; again to go on from where it stopped (see below) |
| **,** / **.** | **<<** | 10 s back / forward |
| **End** | **stop** | back to the live picture (when behind) |
| **I** | **info** | the channel's info: the programme, the streams, the signal |
| **A** | **yellow** | the audio: the next language, or left / right only (for dual-language broadcasts) |
| **S** | **green** | subtitles: on, the next language, off |
| **T** | **text** | teletext |
| **Z** | **blue** | picture shape: as broadcast, full screen, 4:3, 16:9 |
| **R** | **record** | record what's on / stop recording |
| **F** | **red** | the next list (All channels, your lists) |
| **V** | **TV/radio** | radio stations / TV channels |
| **Q** / **Esc** | **exit**, **stop** | stop watching |

**Paused and behind the broadcast.** Pause (Space) keeps the broadcast coming in: when you go on,
you are behind it, and the bar shows by how much ("−1:24", in yellow). **←** and **→** then go
10 s back and forward, **<<** goes back further, **End** (or **stop**) returns to the live picture.
Even without pausing, **<<** goes back into the last couple of minutes. How far back it reaches:
**Settings → Recording** (in memory about 2 minutes; on a USB drive as much as you give it).

**Audio and subtitles.** The channel starts in the first of your languages it has (**Settings →
Picture and sound**). What you choose with **yellow** and **green** is kept for that channel.
Subtitles can be the broadcast's own (DVB) or teletext subtitle pages. Zeonbud's channels have one
audio language and no subtitles now.

**Teletext** (where a channel has it): page 100 first; the digits type a page number, **↑ / ↓**
the next / previous page, the colored keys follow the colored links at the bottom of the page,
**OK** shows the picture under the page, **text** or **exit** closes it. Zeonbud's channels have
no teletext now.

**Radio.** Radio stations have a list of their own (**TV/radio**): the screen shows the station and
what's on.

**Recording.** **record** records the channel until the programme ends (**Settings → Recording**:
or for a fixed time, or until stopped); **● REC** shows at the top. Recordings go to the first USB
drive, or the box's own memory without one (about an hour and a half fits there), into a
**Recordings** folder. While recording, only channels on the same frequency can be watched (the
box has one tuner); the player says so. The **Recordings** list in the TV window plays them (with
seeking, like any video); **Menu** or **F2** on one deletes it.

**Timers.** In the guide, **Enter** on a programme: **Record** (once, every day, or every week),
or **Switch to it when it starts**. A timer starts a minute early and ends five minutes late
(**Settings → Recording**). Timers work with the TV window closed and while the box sleeps (a
recording then runs with the screen off). They don't work after **Power off**: only the remote
turns the box on. Two recordings at the same time on different frequencies conflict: the second
one isn't set, and the window says which one is in the way. The **Timers** list shows them all;
**Enter** cancels one.

**Parental control** (**Settings → Parental control**). Locked channels, and programmes rated at or
above the age you choose (where the broadcast gives a rating), ask for the PIN before they play:
type it with the digits. The PIN is **0000** until you change it. **Lock the TV menu** asks for it
before the settings, scans and editing too.

**Antenna power** (**Settings → Antenna**): 5 V through the antenna cable, for an active antenna with
an amplifier that has no power supply of its own. The same page shows the signal of every
frequency when it was last tuned.

**Backup** (**Settings → Channels**): **Back up to USB** saves the channel list, your lists, the
settings, timers and guide into an `ozone-tv-backup-<date>` folder on the first USB drive;
**Restore from USB** takes the newest one back. **Reset TV data** deletes everything but the
recordings, to start again with a scan.

Only one TV window can run at a time: opening TV again shows it. If you open TV during a scan, the
panel says "TV is busy: a channel scan is running".

A channel gets a 🔒 if it turns out to be encrypted when you try to watch it.

**No signal.** If the tuner can't catch a channel's signal, the player says "no signal, trying
again…": it keeps trying, as the stock TV app does, and the picture starts as soon as the signal is
caught. That can take from a few seconds to a minute or more: here the 1+1, ICTV, СТБ, Інтер
multiplex (506 MHz) is often hard to catch at night. Switch to another channel, or **exit** stops
it. If the signal drops while you watch, the picture stands still and goes on by itself once it is
back. A channel that never comes, day or night, means that frequency isn't received: check the
antenna.

## 7. The browser

Open it with **Super+W** or the globe on the panel. It takes about 4 seconds to start. Closing it
with **Super+Q** quits it and frees its memory, and next time it reopens with the same tabs.

Type an address (`example.com`) or just words into the address bar. Words are searched with
DuckDuckGo.

| Keys | Action |
|---|---|
| **Ctrl+L** or **F6** | go to the address bar |
| **Ctrl+T** | new tab |
| **Ctrl+W** | close the tab |
| **Ctrl+Tab** / **Ctrl+Shift+Tab** | next / previous tab |
| **Alt+←** / **Alt+→** | back / forward |
| **F5** or **Ctrl+R** | reload |
| **Esc** | stop loading / close page search |
| **Ctrl+F** | find on the page |
| **Ctrl++** / **Ctrl+−** / **Ctrl+0** | zoom in / zoom out / normal size |
| **F7** (on the remote: **pause** or **unpause**) | play the video on this page |
| **F8** | fast mode for this site |
| **F9** | reader view |
| **F11** | full screen |

**The toolbar buttons** are, from left to right: back, forward, reload, home, then the address bar,
and then:
- **Play video** (play circle, F7): plays the video on the page in the video player, through the
  hardware decoder (section 5). It finds a video built into the page, a player from another site
  (a YouTube video in a news article, say), or asks yt-dlp about the page itself, which knows over
  a thousand sites. A plain video file starts in about 2 seconds, the others in 10-15.
- **Fast mode** (lightning, F8): turns off the site's JavaScript. Pages load and scroll many times faster.
  Many sites have it on by default. If a site doesn't work properly (buttons, menus, comments),
  turn fast mode off for it.
- **Reader view** (book, F9): just the article text in large type, without anything else. It's handy
  for reading news. Press F9 again to go back.
- **Shield:** ads, trackers and other sites' scripts are blocked. If something important is
  missing on a site (a login button, a video player, a map), click the shield to allow them
  for that site.

**Video in the browser isn't played by the browser itself:** a YouTube link opens in the YouTube
window (section 4) through the hardware decoder, and other videos open in the video player with
the play button (F7). Some sites (Vimeo, social networks) only give their videos to signed-in
users: sign in to the site in the browser first, and the player gets the browser's sign-in.

## 8. Picture and sound settings

**Display and sound** in the app menu (**home** on the remote) has:

- **Resolution**: **Auto** (as at power-on: HDMI if a TV answers, else the analog output), or one of
  the modes the TV says it takes (1080p and 720p at 60 or 50 Hz, 576p, 480p), or **Analog** (PAL or
  NTSC, for a TV on the yellow composite plug). A new mode has to be kept within 15 s with **Keep
  it**; otherwise the old one comes back, so a TV that can't show it is no problem.
- **Match the frame rate of videos**: while a video plays, the output switches to 50 Hz for 25 and
  50 fps videos (European TV, most films from here) and to 60 Hz for 30 and 60 fps ones, so the
  motion is even; it switches back afterwards. The TV goes dark for a second or two at each
  switch, which is why it is off to begin with.
- **Sound output**: **Auto** (HDMI; the analog jack when the picture goes out analog), **HDMI**,
  or the **analog jack**.
- **Digital sound**: **PCM** (the box decodes everything), or AC-3 and DTS as they are, for an AV
  receiver that decodes them: **Auto** (only if the TV or receiver says it takes them) or
  **Always**. Not tried with a receiver yet; with the TV's own speakers keep PCM.
- **Equalizer…**: ten bands from 31 Hz to 16 kHz, for all the sound. **← / →** choose a band,
  **↑ / ↓** its level, the digits a preset (bass, treble, voice, night…), **OK** turns it on and
  off. Levels change at once; turning it on or off takes effect from the next thing that starts
  playing.
- **Screen edges (overscan)…**: the picture's edges, for a TV that cuts them off.

**Volume:**

| How | Action |
|---|---|
| keyboard volume / mute keys, the remote's **vol+ / vol−**, **mute** | volume ±5 %, mute |
| wheel over the 🔈 percentage on the panel | volume |
| click on 🔈 | mute on/off (the level comes back on the next click) |
| **↑ / ↓**, **M** in the player | volume, mute |

## 9. When something goes wrong

| Problem | What to do |
|---|---|
| The keyboard stopped responding | Unplug it and plug it back in (into any USB port). If that doesn't help, use the mouse: ⏻ → **Restart desktop**. |
| Keys don't do what they should (letters trigger actions, text gets selected) | Press **Super+Esc**, which reloads the key bindings. Failing that, ⏻ → **Restart desktop**. |
| A window doesn't respond | **Super+Q**. If it doesn't close, ⏻ → **Restart desktop**. |
| The video stopped on **Loading…** | The connection is slow. Wait, or lower the quality in YouTube (section 4). |
| A black screen with no panel after the video | Press **Q** or **Esc**, which quits a stuck player. If that doesn't help, ⏻ → **Restart desktop**. |
| A black screen after turning on | Wait a minute. Failing that, unplug the box and plug it back in. |
| The screen went dark by itself | It's asleep. Press a key or click the mouse. |
| Everything is slow | Close unneeded browser tabs. If the temperature on the panel is high, give the box ventilation. |
| Games or videos show a black window, or the desktop looks wrong | The graphics chip may be at fault. ⏻ → **Reboot**: if the desktop fails with it, the box falls back to drawing without it by itself. |
| No internet (no IP address or WiFi name on the panel) | Click **WiFi** on the panel and choose your network. If it doesn't connect, check the router, then ⏻ → **Reboot**. |
| "Could not connect …: wrong password?" | Choose the network again and retype the password (letter case matters). |

## 10. The remote control

The box's own remote can run everything by itself: point it at the box.

| Button | What it does |
|---|---|
| **Power** | sleep; any button wakes the box again |
| **↑ ↓ ← →**, **OK** | move and choose, like the arrows and Enter |
| **exit** | back: closes a menu, the on-screen keyboard or the channel list, stops the player, closes the TV window; any other window (YouTube, the browser) it closes |
| **home** | the open windows and all the apps (YouTube, TV, the browser, Power, Screen size, WiFi…) |
| **browse** | the browser |
| **usb** | the drives and network folders (section 11) |
| **info** | the panel, over a full-screen window too (in mouse mode: the right mouse button); in the player, the video's info (TV: the channel's) |
| red | YouTube; in the player, repeat; while watching TV, the next list of channels |
| yellow, green, blue | in YouTube: the next tab, add to a playlist, remove (section 4); in the player and on TV: audio, subtitles, picture shape; in the TV window, green is the next list and blue the channel's menu |
| **epg**, **dvb**, **TV/radio** | the TV window |
| **vol+ / vol−**, **mute** | volume |
| **page+ / page−** | in the player on TV: next / previous channel; in lists, a page up / down |
| **0–9** | the channel number: in the TV window it selects the channel (OK watches), in the player it switches to it after a moment (or at once with OK) |
| **pause**, **unpause** | pause / play in the player; in the browser, play the video on the page |
| **stop** | stop watching (TV, behind the broadcast: back to the live picture) |
| **<<** | back 10 s in the player (TV too, see section 6) |
| **delete** | deletes a character when typing |
| **mouse** | mouse mode, see below |

**browse**, **red**, **epg**, **dvb** and **TV/radio** open their programs only from the empty desktop:
with a program open they are that program's buttons, and nothing opens over it. Close it with
**exit** first, or use **home**, which always works.
| **text** | the on-screen keyboard, see below; while watching TV, teletext |
| **record** | while watching TV, record what's on |

**Mouse mode** (for the browser, or anything made for a mouse). Press **mouse**: the pointer icon
on the panel lights up green. The arrows now move the pointer, faster the longer you hold them;
**OK** clicks, **info** is the right button, **page+ / page−** scroll. The other buttons work as
usual. Press **mouse** again to go back.

**The on-screen keyboard**, for typing a search or an address. Press **text** (or click the
keyboard icon on the panel): a keyboard appears at the bottom and the icon lights up. Move over it
with the arrows and type the key with **OK**; **delete** erases, the digit buttons type digits.
**УКР / ENG** switches the letters, **⇧** gives one capital letter. **exit** or **Hide** hides it.
What it types goes to the window that was being used, so first choose the search field or the
address bar. It can be clicked with the mouse too.

The **>>**, **|<<**, **>>|** and **menu** buttons send exactly the same signal as **<<** (the
remote is made that way), so they all go back 10 s.

**With the mouse alone** everything works too: the panel buttons start YouTube, the browser, TV,
the app menu and the power menu; a click on the window title in the middle of the panel lists the
open windows; **✕** at the right closes the focused window; the keyboard icon gives the on-screen
keyboard for typing.

## 11. USB drives, SD cards and network folders

**USB drives and SD cards** open by themselves: plug one in (a USB stick, a USB hard disk, a card
in the box's SD slot) and after a second or two the panel says "USB drive NAME connected". Its
files are in the folder **/media/NAME**, where NAME is the drive's label (or something like
`usb-sda1` for a drive without one); a drive with several partitions gives a folder for each.
Drives formatted on Windows or a Mac (FAT32, exFAT, NTFS) can be read and written. Linux drives
(ext4) and CD/DVD images can be read; writing to a Linux drive works where its own permissions
allow it.

The **drive icon** on the panel (to the right of the remote's icons) shows how many drives and
folders are open, and grey when there are none. Click it, press **usb** on the remote, or choose
"Drives and network folders" in the app menu:

| In the list | What it does |
|---|---|
| a drive | **Eject** (wait for "NAME: safe to remove", then pull it out), or **Open in a terminal** |
| a network folder | **Disconnect** (until the box restarts), **Forget this folder** (disconnect it for good), **Open in a terminal** |
| **Add a network folder…** | see below |

Pull a USB stick out only after **Eject**. Pulled out without it, the panel says "…was removed
without Eject": what was being written at that moment may be lost. If Eject says "NAME is in use",
close the program that has a file open there (the player, a terminal standing in that folder).

**Network folders** are the shared folders of a computer or a NAS on the home network (Windows
"sharing", Samba, a router with a USB disk). **Add a network folder…** asks three things:
1. **Address**: the computer and the folder, for example `192.168.1.10/Movies`,
   `\\DESKTOP-PC\Movies` or `smb://nas/Movies/Films`. The computer can be its IP address or its
   name on the network.
2. **User**: the user name on that computer. Leave it empty for a folder open to all.
3. **Password** of that user.

If it connects, the panel says "Network folder Movies connected" and the files are in
**/media/Movies**. The box remembers the folder and connects it again by itself after a restart,
and whenever the network or that computer comes back. If it doesn't connect, a message says why:
a wrong user name or password, no such folder, the computer doesn't answer, or there's no computer
by that name.

## 12. Files: videos, music and photos

**Files** (in the app menu, **Super+E**, or **Open in Files** in the drives menu) shows the places
on the left (Videos, Music, Photos and Downloads in the box, TV recordings, every USB drive, SD
card and network folder, Home) and a folder's files on the right: folders first, then videos 🎬,
music 🎵 and photos 🖼, with their size and date. A video that was stopped says where.

Everything works with the remote:

| Remote | Keys | Action |
|---|---|---|
| **↑ / ↓**, **page+ / page−** | | choose, a page at a time |
| **← / →** | | between the places and the files |
| **OK** | **Enter** | open: a folder, a video, a piece of music, a photo |
| **exit** | **Esc** | unmark, up a folder, then the places, then close |
| **blue** | **Menu**, a right click | the menu: play from the start, a slideshow, info, mark, copy, cut, rename, paste, new folder, mark all, delete, the screensaver |
| **green** | **Insert** | mark / unmark, and on to the next: for copying, moving or deleting several |
| **red** | **Ctrl+C** | copy the marked files (or the one chosen); **Ctrl+X** cuts them, to move |
| **yellow** | **Ctrl+V** | paste them into the folder open now |
| **info** | **I** | about the file: size, dates, the drive's free space; for videos and music the format, length, tracks, tags (title, artist, album…) and chapters; for photos the size in pixels and the camera's details (camera, date taken, exposure, place) |
| | **F2**, **Delete**, **Ctrl+Shift+N**, **Ctrl+A** | rename, delete, new folder, mark all |
| **pause** | **Space** | pause / play the music |
| **stop** | | stop the music |
| **<<** | | the music 10 s back |

**Copying**: mark what you want with **green** (a ✓ appears; or none, for the file the cursor is
on), **red** to copy it, go to another folder or drive and press **yellow**. The progress shows
at the top right ("Copying 1/3 film.mkv: 45 %"); **exit** stops it. To move rather than copy, choose
**Cut** in the menu. A video's subtitle files go with it, and a name that is taken gets "(2)".

**Rename** and **New folder** ask for the name: press **text** on the remote for the on-screen
keyboard, **OK** when it is typed. Renaming a video renames its subtitles too.

**Videos** play in the video player (section 5), from where they were left.

**Music** plays in the window: **OK** on a song plays it and the songs after it in that folder.
The bar at the bottom shows the song, the artist and the time, with buttons: **↓** from the last
file goes down to them, **← / →** between them, **OK** presses one, **↑** or **exit** goes back
to the files. They are previous, 10 s back, pause, 10 s forward, next, stop. The music stops when the window
is closed.

**Photos** open full screen: **← / →** the previous / next one, **OK** a slideshow (again: stop),
**↑ / ↓** turn the photo, **info** (or **I**) its name, size and date, **exit** back to the files.

**The screensaver**: after 10 minutes without a key, the mouse or the remote, a slideshow of the
photos in **Photos** (or the folder chosen with **blue** on a folder → *Use for the screensaver's
photos*), or a clock that moves about the screen if there are none. Any key or the mouse ends it,
and that key does nothing else. Not while a video, music, TV or a game plays. **Screensaver** at
the top of the window: on / off, after how many minutes, how long each photo stays, the folder,
and **Try it**.

## 13. Games

**RetroArch** in the app menu (**Super+D**) plays games of these consoles:

- NES / Famicom (`.nes`);
- Game Boy and Game Boy Color (`.gb`, `.gbc`);
- Game Boy Advance (`.gba`).

Put the game files in the **Games** folder of your home folder, or on a USB drive, then choose
**Load Content** in RetroArch's menu. The box's graphics chip draws the games, so they run smoothly.

| Remote | Game | RetroArch's menu |
|---|---|---|
| arrows | the cross (d-pad) | move |
| **OK** | button A | choose |
| **delete** | button B | back |
| **1** / **2** | Start / Select | |
| **page+** / **page−** | L / R shoulders | |
| **0** | opens the menu (save, load, quit the game) | |
| **exit** | closes RetroArch | |

A USB gamepad works too: plug it in before starting RetroArch.

## 14. The network, the hotspot, VPN, phones

**Network** in the app menu, or a click on the address or WiFi on the panel. The line at the top
shows the box's addresses, its way out to the internet and its DNS servers. **OK** opens a line,
**exit** goes back; where something has to be typed, press **text** on the remote for the on-screen
keyboard (or type on the phone, below).

| Line | What it does |
|---|---|
| **Cable** | **Automatic (DHCP)**: the router gives the address (the usual way). **Fixed address…**: the address with its prefix (`192.168.1.50/24`), the router's address and the DNS servers. **PPPoE…**: for a provider whose cable comes straight into the box, with the login and password from the contract. |
| **WiFi networks (and WPS)** | the WiFi list (section 2) |
| **WiFi address** | automatic, or a fixed address on WiFi, from the next connection |
| **WiFi hotspot** | the box as a WiFi network for phones and laptops, below |
| **VPN** | below |
| **Proxy** | a proxy server (`http://host:port`, with `user:password@` if it needs one) for the browser, YouTube and the player at once; the rest of the programs use it from the next start of the desktop |
| **Remote control on the phone** | below |
| **Receive videos from a phone (DLNA)** | below |

**The WiFi hotspot** shares the box's internet (the cable, PPPoE, or its own WiFi) over WiFi.
**Turn it on**, then join the network it shows ("Ozone-…") with the password shown, on the phone or
laptop. **Name** and **Password** change them. The box stays on its own WiFi network meanwhile.

**VPN** connects through a VPN service or a server of your own. The VPN provider gives a file:
OpenVPN (`.ovpn`) or WireGuard (`.conf`). Put it in a **VPN** folder in your home folder, in
**Downloads** or on a USB drive, then **VPN → Add a VPN file…** (it asks for the login when the file
needs one). **Connect to NAME** sends all the internet through it, with its own DNS servers;
**Disconnect** goes back to the usual way. It stays off after a restart.

**The remote control on the phone**: **Remote control on the phone…** shows a QR code: point the
phone's camera at it (or open the address shown, `http://…:8080`, in the phone's browser). The
phone has to be on the same home WiFi as the box (or on its hotspot). The page has three tabs:
**Remote** (the box's remote: arrows, OK, exit, home, volume, the player's keys, the colour keys),
**Touchpad** (move the pointer with a finger, tap to click, two fingers to scroll) and **Type**
(type on the phone's keyboard, in any language, and send it to the box: into a search box, a
password field…). Add the page to the phone's home screen to have it at hand.

**Sending to the box (DLNA)**: the box shows up as **Ozone TV** in the apps that "cast" or "play
to" a TV: BubbleUPnP and other DLNA apps on Android, VLC, Kodi, a phone's gallery with "Play on
another device", Windows' **Cast to device** (right click on a video). Videos play in the video
player, music and photos full screen; the phone pauses, winds and stops them, and sets the volume,
and the remote works as usual (**exit** stops). **Receive videos from a phone** turns it off or on,
and gives it another name. (YouTube's own cast button needs Google's protocol, which the box
doesn't have.)

**Media servers**: a NAS, Jellyfin, Plex, minidlna or a computer sharing its media library is in
**Files → Media servers** (or **Media servers** in the app menu): choose the server, then its
folders; **OK** plays a video, a piece of music (and the ones after it) or a photo (a slideshow
of that folder), **exit** goes up a folder.

## 15. All key combinations

### Anywhere
| Keys | Action |
|---|---|
| Super+Enter | terminal |
| Super+D | app menu |
| Super+Y | YouTube |
| Super+W | browser |
| Super+T | television |
| Super+Shift+E | power menu (sleep, Android, restart, screen size, power off) |
| Super+Esc | reload the key bindings |
| Super+Q | close the window |
| Super+F | full screen |
| Super+S | floating window / back into the layout |
| Super+M | one window over the whole screen / shared layout |
| Super+H/J/K/L | focus the window left/below/above/right |
| Super+Shift+H/J/K/L | swap the window with its neighbour |
| Super+1…5 | workspace 1…5 |
| Super+Shift+1…5 | move the window to workspace 1…5 |
| Super+Tab | previous workspace |
| Super + left / right mouse drag | move / resize a window |
| volume / mute keys | volume |

### Menus (apps, power, TV channels)
| Keys | Action |
|---|---|
| typing | filter |
| ↑ / ↓ | choose |
| Enter | confirm |
| Esc | close |

### YouTube
| Keys | Action |
|---|---|
| typing + Enter | search |
| arrows, Enter | choose, watch |
| ↓ (in the search box) | to the cards |
| Ctrl+L or / | to the search box |
| Esc | to the cards / close |

### Player
| Keys | Action |
|---|---|
| Space / Enter / P | pause (TV: Enter opens the channel list) |
| ← / → | −10 s / +10 s (TV: previous / next channel; behind the broadcast, −10 s / +10 s) |
| PgDn / PgUp | −1 min / +1 min (TV: previous / next channel) |
| ↑ / ↓ | volume |
| M | mute |
| Q / Esc / Backspace | quit |
| TV only: I, A, S, T, Z, R, F, V, End, `,` `.` | info, audio, subtitles, teletext, picture shape, record, next list, radio, live, −10 s / +10 s |

### TV window
| Keys | Action |
|---|---|
| ↑ / ↓, a click | choose a channel and see its guide |
| Enter, a double click | watch (on a programme: record, timers) |
| → / ← | into the guide / back to the channels |
| Menu / F2, a right click | the channel's menu |
| Tab | the next list |
| Esc | close |

### Browser
| Keys | Action |
|---|---|
| Ctrl+L, F6 | address bar |
| Ctrl+T / Ctrl+W | new / close tab |
| Ctrl+Tab / Ctrl+Shift+Tab | next / previous tab |
| Alt+← / Alt+→ | back / forward |
| F5, Ctrl+R | reload |
| Esc | stop / close search |
| Ctrl+F | find |
| Ctrl++ / Ctrl+− / Ctrl+0 | zoom |
| F8 | fast mode |
| F9 | reader view |
| F11 | full screen |
