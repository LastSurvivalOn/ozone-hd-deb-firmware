#!/bin/bash
# Installs the desktop (bspwm + polybar, Catppuccin) into an already prepared Debian.
# Packages are installed separately (see README); this installs configs, scripts, yt-dlp and X autostart.
# Usage: ./install-desktop.sh [box ip]
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../config.sh"
IP=$(box_ip "${1:-}")

echo ">> system files"
tar -C "$HERE/rootfs" -czf - \
    usr/local/lib/ozone/desktop usr/local/lib/ozone/gen-xorg-input usr/local/lib/ozone/kbd-merge usr/local/lib/ozone/splash \
    usr/local/lib/ozone/hdmi-watch usr/local/lib/ozone/set-mode usr/local/lib/ozone/sound-config etc/init.d/ozone-hw \
    usr/local/bin/ozone-display usr/local/bin/ozone-equalizer usr/local/bin/ozone-files usr/local/bin/ozone-screensaver \
    usr/local/share/applications/ozone-display.desktop usr/local/share/applications/ozone-equalizer.desktop \
    usr/local/share/applications/ozone-files.desktop \
    usr/local/bin/yt usr/local/bin/ozone-web usr/local/bin/ozone-web-show usr/local/share/applications/ozone-web.desktop usr/local/bin/ozone-power usr/local/bin/ozone-screen usr/local/share/applications/ozone-screen.desktop usr/local/bin/ozone-standby usr/local/bin/ozone-notify etc/init.d/ozone-input etc/ozone/remote.conf etc/ozone/native-only \
    usr/local/bin/ozone-wifi usr/local/bin/ozone-volume usr/local/bin/ozone-remote usr/local/bin/ozone-panel usr/local/bin/ozone-media usr/local/share/applications/ozone-media.desktop usr/local/bin/ozone-terminal usr/local/share/applications/ozone-terminal.desktop usr/local/bin/ozone-sysinfo usr/local/share/applications/ozone-sysinfo.desktop etc/init.d/ozone-media usr/local/share/applications/ozone-wifi.desktop usr/local/share/applications/yt.desktop usr/local/share/applications/ozone-tv.desktop usr/local/share/applications/ozone-power.desktop etc/init.d/ozone-wifi usr/local/lib/ozone/wifi-event usr/local/lib/ozone/netwatch \
    usr/local/lib/ozone/gen-xorg-screen etc/X11/Xwrapper.config etc/asound.conf |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'

# what System info shows as the version of these scripts
echo "$(git -C "$HERE" log -1 --format='%h, %cd' --date=format:'%d.%m.%Y' 2>/dev/null || echo unknown)$(git -C "$HERE" diff --quiet HEAD 2>/dev/null || echo ' (with local changes)')" |
    ssh_box 'sudo mkdir -p /etc/ozone && sudo tee /etc/ozone/version > /dev/null'

echo ">> user files"
tar -C "$HERE/dotfiles" -czf - . | ssh_box 'tar -C ~ -xzf - && chmod +x ~/.xinitrc ~/.config/bspwm/bspwmrc ~/.config/polybar/launch.sh'

echo ">> amplay (video through the hardware decoder), built on the box"
# libzvbi decodes TV teletext (its runtime comes with FFmpeg already)
ssh_box 'sudo apt-get install -y -qq --no-install-recommends libzvbi-dev > /dev/null'
tar -C "$HERE/amplay" -czf - amplay.c osd.c osd.h ttx.c ttx.h Makefile |
    ssh_box 'set -e; d=$(mktemp -d); tar -C $d -xzf -; make -s -C $d; sudo make -s -C $d install; rm -r $d'

echo ">> yt-dlp (latest release: the Debian version stopped working with YouTube long ago)"
ssh_box 'sudo curl -fsSL -o /usr/local/bin/yt-dlp https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp &&
         sudo chmod 755 /usr/local/bin/yt-dlp && yt-dlp --version'

echo ">> rclone (network folders over SMB2/3; Debian's 1.60 litters shares with a folder named \"．\")"
ssh_box 'set -e; d=$(mktemp -d)
         curl -fsSL -o $d/rclone.zip https://downloads.rclone.org/rclone-current-linux-arm-v7.zip
         python3 -I -c "import sys, zipfile; z = zipfile.ZipFile(sys.argv[1]); n = [x for x in z.namelist() if x.endswith(\"/rclone\")][0]; open(sys.argv[2], \"wb\").write(z.read(n))" $d/rclone.zip $d/rclone
         sudo install -m 755 $d/rclone /usr/local/bin/rclone; rm -r $d; rclone version | head -1'

echo ">> permissions and autostart"
ssh_box 'set -e
    sudo usermod -a -G video,audio,input,tty "$(id -un)"
    # sysvinit starts X and restarts it if it crashes; under Android inittab is not used
    grep -q ozone/desktop /etc/inittab ||
        echo "X0:2:respawn:/usr/local/lib/ozone/desktop" | sudo tee -a /etc/inittab > /dev/null
    sudo mkdir -p /etc/X11/xorg.conf.d
    # keyboard hotplug without udev (see /usr/local/lib/ozone/kbd-merge)
    sudo apt-get install -y -qq --no-install-recommends python3-evdev > /dev/null
    sudo update-rc.d ozone-input defaults
    # WiFi (wpa_supplicant; the networks are chosen with ozone-wifi)
    sudo apt-get install -y -qq --no-install-recommends wpasupplicant > /dev/null
    sudo update-rc.d ozone-wifi defaults
    # drives and network folders (ozone-media: FAT, NTFS, exFAT, ext; cifs-utils for SMB1 servers)
    sudo apt-get install -y -qq --no-install-recommends ntfs-3g exfat-fuse cifs-utils > /dev/null
    sudo update-rc.d ozone-media defaults
    # the equalizer (sound-config puts it in /etc/asound.conf when it is turned on)
    sudo apt-get install -y -qq --no-install-recommends libasound2-plugin-equal caps > /dev/null
    # the folders Files starts with
    mkdir -p ~/Videos ~/Music ~/Pictures
    # ozone-web opens links from other programs
    sudo update-alternatives --install /usr/bin/x-www-browser x-www-browser /usr/local/bin/ozone-web 60 > /dev/null
    fc-list : family | grep -i -E "jetbrains|fontawesome" | sort -u'

# remotecfg for the IR remote (and the TV's libraries): copies in Debian, not Android's partition
"$HERE/install-android-files.sh" "$IP"
echo ">> done; start without rebooting: sudo telinit q"
