#!/bin/bash
# First-time setup of Debian inside the chroot on the box.
# Usage: debian-setup.sh <user> <public ssh key file>
set -euo pipefail

USER_NAME=$1
PUBKEY_FILE=$2
export DEBIAN_FRONTEND=noninteractive

# The kernel is built with ANDROID_PARANOID_NETWORK: only members of these groups may use sockets
groupadd -f -g 3003 aid_inet
groupadd -f -g 3004 aid_net_raw
groupadd -f -g 3005 aid_net_admin
groupadd -f -g 1015 aid_sdcard_rw
groupadd -f -g 1028 aid_sdcard_r
groupadd -f -g 1004 aid_input
# apt drops supplementary groups and keeps only the primary one
usermod -g aid_inet _apt

# The Docker image disables fsync in dpkg; on NAND without a UPS that leads to a corrupted system
rm -f /etc/dpkg/dpkg.cfg.d/docker-apt-speedup

apt-get update
apt-get install -y --no-install-recommends \
    sysv-rc dropbear openssh-sftp-server sudo ca-certificates curl nano htop procps \
    iproute2 iputils-ping less tzdata cron

ln -sf /usr/share/zoneinfo/Europe/Kyiv /etc/localtime
echo Europe/Kyiv > /etc/timezone

echo ozone > /etc/hostname
printf '127.0.0.1\tlocalhost\n127.0.1.1\tozone\n::1\t\tlocalhost\n' > /etc/hosts

# New users get network access right away
sed -i -e 's/^#\?ADD_EXTRA_GROUPS=.*/ADD_EXTRA_GROUPS=1/' \
       -e 's/^#\?EXTRA_GROUPS=.*/EXTRA_GROUPS="aid_inet aid_net_raw aid_sdcard_rw aid_sdcard_r"/' \
       /etc/adduser.conf

if ! id "$USER_NAME" >/dev/null 2>&1; then
    useradd -m -s /bin/bash -G sudo,aid_inet,aid_net_raw,aid_sdcard_rw,aid_sdcard_r,aid_input "$USER_NAME"
fi
usermod -p '*' "$USER_NAME"
echo "$USER_NAME ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/$USER_NAME
chmod 440 /etc/sudoers.d/$USER_NAME
install -d -m 700 -o "$USER_NAME" -g "$USER_NAME" /home/$USER_NAME/.ssh
install -m 600 -o "$USER_NAME" -g "$USER_NAME" "$PUBKEY_FILE" /home/$USER_NAME/.ssh/authorized_keys

# Dropbear instead of OpenSSH: the sshd sandbox forbids opening files, and without getrandom()
# (missing in kernel 3.10) glibc reads entropy from /dev/urandom and crashes. -s: no passwords, -w: no root
sed -i 's/^#\?DROPBEAR_EXTRA_ARGS=.*/DROPBEAR_EXTRA_ARGS="-s -w"/' /etc/default/dropbear
grep -q '^DROPBEAR_EXTRA_ARGS=' /etc/default/dropbear || echo 'DROPBEAR_EXTRA_ARGS="-s -w"' >> /etc/default/dropbear

# Let apt start services after installation, as in regular Debian
rm -f /usr/sbin/policy-rc.d
update-rc.d dropbear enable
update-rc.d cron enable

apt-get clean
echo "Debian configured"
