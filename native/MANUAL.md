# OzoneHD box: user manual

How to use the box in native Linux: the desktop, YouTube, TV, the browser, the video player,
sleep and Android. You need a USB keyboard and a mouse. There's no remote.

**Super** is the key with the Windows logo. "Super+Q" means hold Super and press Q.

## Contents

1. [Turning on, sleep, power off, Android](#1-turning-on-sleep-power-off-android)
2. [The desktop and the panel](#2-the-desktop-and-the-panel)
3. [Windows and workspaces](#3-windows-and-workspaces)
4. [YouTube](#4-youtube)
5. [The video player](#5-the-video-player)
6. [Television (DVB-T2)](#6-television-dvb-t2)
7. [The browser](#7-the-browser)
8. [Sound](#8-sound)
9. [When something goes wrong](#9-when-something-goes-wrong)
10. [All key combinations](#10-all-key-combinations)

---

## 1. Turning on, sleep, power off, Android

**Turning on.** Plug the box in. The pulsing Ozone HD logo shows while Linux starts, until the desktop
appears.

**The power menu** is **Super+Shift+E** or the ⏻ button at the right end of the panel. Choose an
item with the arrows and Enter, or click it. Esc closes the menu.

| Item | What it does |
|---|---|
| Sleep | Turns the screen off. Press any key or click a mouse button to wake it. Waking is instant. The network keeps working while it sleeps. Quit the video player first: it doesn't stop by itself. |
| Android | Closes Linux and starts the original Android (the stock TV app, SmartTube). |
| Restart desktop | Restarts the graphical session. Open windows close. Use it when something hangs (see section 9). |
| Reboot | Restarts the box into Linux. |
| Power off | Stops the system and turns off the screen. The box itself can't cut its own power, so it stays powered until you unplug it. To turn it on again, unplug it and plug it back in. |

**From Android back to Linux:** restart the box from Android, or unplug it and plug it back in.
Linux always starts by default.

## 2. The desktop and the panel

The panel is the bar along the top of the screen. From left to right:

| Element | Click |
|---|---|
| ▦ (grid) | app menu |
| YouTube button | YouTube search (section 4) |
| Globe | the browser (section 7) |
| Screen icon | television (section 6) |
| 1 2 3 4 5 | workspaces (section 3). The current one is highlighted; the ones with windows are coloured. |
| yellow text | short messages: "Loading video…", "Scan finished", errors. They disappear after 6 s. |
| centre | the title of the active window |
| CPU %, memory, temperature | system load |
| IP address | network status. It turns red ("offline") when there's no network. |
| 🔈 percentage | volume. Click to mute, scroll the wheel over it to change the volume. |
| date and time | |
| ⏻ | power menu |

**The app menu** opens with **Super+D** or the grid button. Start typing a name ("term", "files"…)
and the list filters as you type. Use the arrows to choose and Enter to launch. Esc closes it.

**The terminal** opens with **Super+Enter**.

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

**Searching.** Type a query and press Enter. Results arrive in about a second, as cards showing:
- the thumbnail, with the length in the corner (a red **LIVE** marks a live broadcast);
- the channel's avatar and name;
- the title;
- the number of views and how long ago it was published.

You can start typing anywhere in the window, not only in the search box: the text goes straight
into it. A YouTube link pasted into the search box plays at once.

| Keys / mouse | Action |
|---|---|
| typing + **Enter** | new search |
| **arrows** | choose a card |
| **Enter** or a click on a card | watch |
| **↓** in the search box | go to the cards |
| **Esc** | from the search box: go to the cards; from the cards: close YouTube |
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

The player is used by YouTube, TV and local files. Moving the mouse shows the controls:
- at the top: the title and ✕ (close);
- at the bottom: the buttons, the time, the progress bar and the volume.

They hide again after 3 s.

| Keys | Action |
|---|---|
| **Space**, **Enter** or **P** | pause / play |
| **←** / **→** | back / forward 10 s |
| **PgDn** / **PgUp** | back / forward 1 minute |
| **↑** / **↓** | volume up / down |
| **M** | mute on/off |
| **Q**, **Esc** or **Backspace** | quit |

| Mouse | Action |
|---|---|
| click on the picture | pause / play |
| click on the progress bar | jump to that point |
| ⏪ ⏯ ⏩ buttons | back 10 s / pause / forward 10 s |
| click on the volume | mute |
| wheel | volume |
| ✕ at the top right | quit |

**Loading…** in place of the time means the player is waiting for data from the network. It
continues by itself once a few seconds are buffered. Multimedia keys (play, pause, next…) on a
keyboard that has them do the same as the keys above.

## 6. Television (DVB-T2)

Open it with **Super+T** or the screen icon on the panel. An antenna must be connected to the box.

**The first time**, choose **⟳ Scan channels** at the end of the list. The scan checks every
frequency, which takes 2–3 minutes. When it finishes, the panel shows "Scan finished: N channels",
and the list fills up. Scan again if you move the antenna or channels change.

**Watching:** choose a channel in the list (type part of the name to filter it) and press Enter.

| Keys | Action |
|---|---|
| **→** or **PgUp** | next channel |
| **←** or **PgDn** | previous channel |
| **↑ / ↓**, **M** | volume, mute |
| **Q** / **Esc** | stop watching |

Channels marked 🔒 are encrypted and won't play. "No signal" on the panel means that frequency
isn't received, so check the antenna.

What isn't available: programme guide (EPG), teletext and subtitles, recording, radio.

## 7. The browser

Open it with **Super+W** or the globe on the panel. It opens instantly, because it stays ready in
the background. Closing it with **Super+Q** hides it, and next time it reopens with the same
tabs.

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
| **F8** | fast mode for this site |
| **F9** | reader view |
| **F11** | full screen |

**The toolbar buttons** are, from left to right: back, forward, reload, home, then the address bar,
and then:
- **Fast mode** (lightning, F8): turns off the site's JavaScript. Pages load and scroll many times faster.
  Many sites have it on by default. If a site doesn't work properly (buttons, menus, comments),
  turn fast mode off for it.
- **Reader view** (book, F9): just the article text in large type, without anything else. It's handy
  for reading news. Press F9 again to go back.
- **Shield:** ads, trackers and other sites' scripts are blocked. If something important is
  missing on a site (a login button, a video player, a map), click the shield to allow them
  for that site.

**Video in the browser isn't played by the browser itself:** a YouTube link opens in the YouTube
window (section 4) through the hardware decoder.

## 8. Sound

Sound comes out of the TV over HDMI.

| How | Action |
|---|---|
| keyboard volume / mute keys | volume ±5 %, mute |
| wheel over the 🔈 percentage on the panel | volume |
| click on 🔈 | mute on/off |
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
| No internet (red "offline" on the panel) | Check the WiFi router, then ⏻ → **Reboot**. |

## 10. All key combinations

### Anywhere
| Keys | Action |
|---|---|
| Super+Enter | terminal |
| Super+D | app menu |
| Super+Y | YouTube |
| Super+W | browser |
| Super+T | television |
| Super+Shift+E | power menu (sleep, Android, restart, power off) |
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
| Space / Enter / P | pause |
| ← / → | −10 s / +10 s (TV: previous / next channel) |
| PgDn / PgUp | −1 min / +1 min (TV: previous / next channel) |
| ↑ / ↓ | volume |
| M | mute |
| Q / Esc / Backspace | quit |

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
