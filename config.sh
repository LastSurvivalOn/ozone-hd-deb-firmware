# The settings of this computer and box, sourced by the scripts: .env next to this file (a copy of
# .env.example, which git ignores), else the defaults here.
CONFIG_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
[ -f "$CONFIG_ROOT/.env" ] && . "$CONFIG_ROOT/.env"

# the box's Debian user: hybrid/install.sh makes it with the name you have here
BOX_USER=${BOX_USER:-$(id -un)}
# the box's address on the cable, and over WiFi (both best as DHCP reservations on the router)
BOX_IP=${BOX_IP:-}
BOX_WIFI_IP=${BOX_WIFI_IP:-}
# the key hybrid/install.sh makes and puts on the box
SSH_KEY=${SSH_KEY:-$HOME/.ssh/id_ed25519_ozone}
SSH_KEY=${SSH_KEY/#\~/$HOME}

# The box's IP: the script's argument, else BOX_IP
box_ip() {
    echo "${1:-${BOX_IP:?give the box IP as the first argument, or set BOX_IP in .env (see .env.example)}}"
}

# ssh and scp to the box at $IP. Over WiFi it is the same host, so its key is the one known for the
# cable address.
ssh_box() { ssh -i "$SSH_KEY" -o ConnectTimeout=10 ${BOX_IP:+-o HostKeyAlias=$BOX_IP} "$BOX_USER@$IP" "$@"; }
scp_box() { scp -q -i "$SSH_KEY" ${BOX_IP:+-o HostKeyAlias=$BOX_IP} "$@"; }
