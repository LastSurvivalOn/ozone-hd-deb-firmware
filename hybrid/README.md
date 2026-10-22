# OzoneHD T2: stripped-down Android + Debian server

Android 4.4 stays as the base system (kernel, drivers, T2, remote). Next to it, Debian 12 armhf
lives in an image file `/data/linux/debian.img` (ext4, 2 GB) and starts on its own after boot.

## Login

```sh
ssh -i ~/.ssh/id_ed25519_ozone <USER>@<BOX_IP>   # passwordless sudo
adb connect <BOX_IP>:5555                        # Android; the firmware enables network adb by itself
```

The IP comes from the router. It's worth reserving one for the box in the router's DHCP settings.
The scripts take the box's address, user and key from `.env` in the repository's root: copy
`.env.example` to `.env` and fill it in (git ignores it). A box IP given to a script as its first
argument wins over it.

## Scripts (run from the laptop)

| script | what it does |
|---|---|
| `install.sh [2G]` | creates the image, unpacks Debian, installs base packages and the user; safe to re-run |
| `debloat.sh` / `--undo` | disables system bloat, removes third-party apps, installs SmartTube |
| `autostart.sh` / `--undo` | appends the Debian start hook to `/system/bin/preinstall.sh` (original: `/data/linux/preinstall.sh.orig`) |

On the box: `su -c '/data/linux/ozlinux.sh start|stop|restart|shell|status'`, startup log: `/data/linux/boot.log`.

## Services in Debian

There is no systemd (kernel 3.10 has no cgroups), so plain sysv scripts are used:
`apt install <package>` and `sudo update-rc.d <service> enable`. At startup `/etc/ozone/rc`
starts everything in `/etc/rc2.d`.

## Kernel quirks already handled

- `ANDROID_PARANOID_NETWORK`: the network is available only to the `aid_inet` group (3003). New users
  get it automatically via `adduser`; for service users: `usermod -a -G aid_inet <user>`.
  If a service drops supplementary groups (like apt does), `aid_inet` must be its primary group.
- No `getrandom()` and no seccomp: the OpenSSH server crashes, so Dropbear is used instead
  (`/etc/default/dropbear`, `-s -w`: no passwords and no root). sftp/scp work.
- Docker is impossible (no cgroups/namespaces).
- Android's internal storage is visible in Debian as `/mnt/sdcard`.

## Rollback

- Android: `./debloat.sh --undo`, `./autostart.sh --undo`.
- Full: partitions from `~/ozone_backup/backup/*.img`. The only things deleted from `/system` are the dead
  `org.xbmc.xbmc.tar.gz`, `other.tar.gz`, `DragonAgingMain.apk`, `test.apk`.
