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
    usr/local/share/applications/ozone-files.desktop usr/local/bin/ozone-photos \
    usr/local/share/applications/ozone-photos.desktop \
    usr/local/bin/ozone-calendar usr/local/share/applications/ozone-calendar.desktop \
    usr/local/bin/ozone-code usr/local/share/applications/ozone-code.desktop usr/local/share/ozone/geany \
    usr/local/bin/ozone-system usr/local/share/applications/ozone-updates.desktop \
    usr/local/bin/ozone-calc usr/local/bin/ozone-math usr/local/share/applications/ozone-calc.desktop \
    usr/local/bin/ozone-apps usr/local/share/applications/ozone-apps.desktop \
    usr/local/bin/ozone-wallpaper usr/local/share/applications/ozone-wallpaper.desktop usr/local/share/ozone/wallpapers \
    usr/local/bin/ozone-screenshot usr/local/share/applications/ozone-screenshot.desktop \
    usr/local/share/applications/ozone-math.desktop usr/local/share/ozone/wxMaxima.conf \
    etc/dpkg/dpkg.cfg.d/ozone-maxima \
    usr/local/share/applications/ozone-backup.desktop etc/init.d/ozone-system \
    usr/local/bin/yt usr/local/bin/ozone-web usr/local/bin/ozone-web-show usr/local/share/applications/ozone-web.desktop usr/local/bin/ozone-power usr/local/bin/ozone-screen usr/local/share/applications/ozone-screen.desktop usr/local/bin/ozone-standby usr/local/bin/ozone-notify etc/init.d/ozone-input etc/ozone/remote.conf etc/ozone/native-only \
    usr/local/bin/ozone-wifi usr/local/bin/ozone-volume usr/local/bin/ozone-remote usr/local/bin/ozone-panel usr/local/bin/ozone-media usr/local/share/applications/ozone-media.desktop usr/local/bin/ozone-terminal usr/local/share/applications/ozone-terminal.desktop usr/local/bin/ozone-sysinfo usr/local/share/applications/ozone-sysinfo.desktop etc/init.d/ozone-media usr/local/share/applications/ozone-wifi.desktop usr/local/share/applications/yt.desktop usr/local/share/applications/ozone-tv.desktop usr/local/share/applications/ozone-power.desktop etc/init.d/ozone-wifi usr/local/lib/ozone/wifi-event usr/local/lib/ozone/netwatch \
    usr/local/lib/ozone/gen-xorg-screen etc/X11/Xwrapper.config etc/asound.conf |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'

# what System info shows as the version of these scripts
echo "$(git -C "$HERE" log -1 --format='%h, %cd' --date=format:'%d.%m.%Y' 2>/dev/null || echo unknown)$(git -C "$HERE" diff --quiet HEAD 2>/dev/null || echo ' (with local changes)')" |
    ssh_box 'sudo mkdir -p /etc/ozone && sudo tee /etc/ozone/version > /dev/null'
# and the commit, which Updates compares with GitHub's
git -C "$HERE" rev-parse HEAD 2>/dev/null | ssh_box 'sudo tee /etc/ozone/commit > /dev/null'

echo ">> amplay (video through the hardware decoder), built on the box, and the user files"
# libzvbi decodes TV teletext (its runtime comes with FFmpeg already)
ssh_box 'sudo apt-get install -y -qq --no-install-recommends libzvbi-dev > /dev/null'
# The user files go through update.py, as the box's own updates do: one the user changed is kept.
# A file is still ours if it is a version from git history (known.sha256), or the one installed last.
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/amplay"
cp -a "$HERE/dotfiles" "$HERE/update.py" "$W/"
cp "$HERE"/amplay/{amplay.c,osd.c,osd.h,ttx.c,ttx.h,Makefile} "$W/amplay/"
(cd "$HERE/dotfiles" && find . -type f | sed 's|^\./||') | while read -r f; do
    git -C "$HERE" log --format=%H -- "dotfiles/$f" 2>/dev/null | while read -r c; do
        h=$(git -C "$HERE" show "$c:native/dotfiles/$f" 2>/dev/null | sha256sum | cut -d' ' -f1)
        echo "$h  $f"
    done
done | sort -u > "$W/known.sha256"
tar -C "$W" -czf - . | ssh_box 'set -e; d=$(mktemp -d); tar -C $d -xzf -
    make -s -C $d/amplay; sudo make -s -C $d/amplay install
    sudo python3 -I $d/update.py --native $d --user "$(id -un)" --dotfiles-only --known $d/known.sha256 --amplay-built
    rm -r $d'

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
    # drives and network folders (ozone-media: FAT, NTFS, exFAT, ext; cifs-utils for SMB1 servers;
    # sfdisk, mkfs.vfat and mkfs.exfat to format a drive)
    sudo apt-get install -y -qq --no-install-recommends ntfs-3g exfat-fuse cifs-utils fdisk dosfstools \
        exfatprogs > /dev/null
    sudo update-rc.d ozone-media defaults
    # a daemon still running keeps its old code (an old one mounts a drive while it is formatted)
    if /etc/ozone/is-native; then sudo /etc/init.d/ozone-media restart; fi
    # the equalizer (sound-config puts it in /etc/asound.conf when it is turned on)
    sudo apt-get install -y -qq --no-install-recommends libasound2-plugin-equal caps > /dev/null
    # the photo viewer (ozone-photos) draws with cairo from Python; phones and sites give WebP
    sudo apt-get install -y -qq --no-install-recommends python3-gi-cairo webp-pixbuf-loader > /dev/null
    # the code editor (ozone-code): Geany with its file tree, and C++ beside the C compiler amplay needs
    sudo apt-get install -y -qq --no-install-recommends geany geany-plugin-treebrowser g++ > /dev/null
    # Updates and backups (ozone-system): zstd packs the whole system in four threads; the settings
    # restored and the reset happen at the start, before the network and Bluetooth (ozone-system)
    sudo apt-get install -y -qq --no-install-recommends zstd > /dev/null
    # Programs (ozone-apps) compares Debian's versions with apt_pkg
    sudo apt-get install -y -qq --no-install-recommends python3-apt > /dev/null
    # the calculator (Qalculate!) and maths (wxMaxima: Maxima with its share library and its manual,
    # which etc/dpkg/dpkg.cfg.d/ozone-maxima lets in); gnuplot draws the plots of both
    sudo apt-get install -y -qq --no-install-recommends qalculate-gtk gnuplot-x11 wxmaxima maxima \
        maxima-share maxima-doc > /dev/null
    # GCL compiles Maxima'"'"'s draw package the first time it loads: 4 minutes, once
    [ -d ~/.maxima/binary ] || maxima --very-quiet --batch-string="load(draw)\$" > /dev/null 2>&1
    sudo update-rc.d ozone-system defaults
    # the folders Files starts with
    mkdir -p ~/Videos ~/Music ~/Pictures
    # ozone-web opens links from other programs
    sudo update-alternatives --install /usr/bin/x-www-browser x-www-browser /usr/local/bin/ozone-web 60 > /dev/null
    fc-list : family | grep -i -E "jetbrains|fontawesome" | sort -u'

# remotecfg for the IR remote (and the TV's libraries): copies in Debian, not Android's partition
"$HERE/install-android-files.sh" "$IP"
echo ">> done; start without rebooting: sudo telinit q"
