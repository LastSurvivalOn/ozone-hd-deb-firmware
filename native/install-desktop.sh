#!/bin/bash
# Installs the desktop (bspwm + polybar, Catppuccin) into an already prepared Debian.
# Packages are installed separately (see README); this installs configs, scripts, yt-dlp and X autostart.
# Usage: ./install-desktop.sh <box ip>
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
IP=${1:?specify the box IP}
KEY=$HOME/.ssh/id_ed25519_ozone
ssh_box() { ssh -i $KEY -o ConnectTimeout=10 <USER>@$IP "$@"; }

echo ">> system files"
tar -C "$HERE/rootfs" -czf - \
    usr/local/lib/ozone/desktop usr/local/lib/ozone/gen-xorg-input usr/local/lib/ozone/kbd-merge \
    usr/local/bin/yt usr/local/bin/ozone-power usr/local/bin/ozone-standby usr/local/bin/ozone-notify etc/init.d/ozone-input etc/ozone/native-only \
    etc/X11/xorg.conf.d/00-ozone-screen.conf etc/X11/Xwrapper.config etc/asound.conf |
    ssh_box 'sudo tar -C / --no-same-owner -xzf -'

echo ">> user files"
tar -C "$HERE/dotfiles" -czf - . | ssh_box 'tar -C ~ -xzf - && chmod +x ~/.xinitrc ~/.config/bspwm/bspwmrc ~/.config/polybar/launch.sh'

echo ">> yt-dlp (latest release: the Debian version stopped working with YouTube long ago)"
ssh_box 'sudo curl -fsSL -o /usr/local/bin/yt-dlp https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp &&
         sudo chmod 755 /usr/local/bin/yt-dlp && yt-dlp --version'

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
    fc-list : family | grep -i -E "jetbrains|fontawesome" | sort -u'
echo ">> done; start without rebooting: sudo telinit q"
