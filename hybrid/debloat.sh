#!/bin/bash
# Cleans up Android on the box: disables system bloat (reversible: pm enable <pkg>),
# removes third-party apps, installs SmartTube and tweaks a few settings.
# Usage: ./debloat.sh            — run
#        ./debloat.sh --undo     — re-enable everything that was disabled
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)

# Kept: T2 (th.dtv, DTVLauncher, SubTitle), remote (RC_Server, RemoteIME),
# Settings, SystemUI, Keyguard/Phone (4.4 crashes without them), file manager, VideoPlayer.
DISABLE=(
    # Google
    com.android.vending com.google.android.gms com.google.android.gsf com.google.android.gsf.login
    com.google.android.googlequicksearchbox com.google.android.partnersetup
    com.google.android.backuptransport com.google.android.feedback com.google.android.gm
    com.google.android.ears com.google.android.apps.genie.geniewidget com.google.android.apps.uploader
    com.google.android.marvin.talkback com.google.android.tts com.google.android.onetimeinitializer
    com.google.android.syncadapters.calendar com.google.android.syncadapters.contacts
    com.google.tv.discovery com.google.tv.ipremote
    # Amlogic / factory
    com.amlapp.update.otaupgrade com.amlogic.miracast com.amlogic.mediacenter com.amlogic.PPPoE
    com.amlogic.promptuser com.softwinner.agingdragonbox com.softwinner.dragonbox
    com.dzj.installapp com.gsoft.appinstall
    # AOSP apps that have no business on a TV box
    com.android.calculator2 com.android.calendar com.android.providers.calendar com.android.camera2
    com.android.deskclock com.android.email com.android.exchange com.android.facelock
    com.android.gallery3d com.android.htmlviewer com.android.music com.android.musicfx
    com.android.printspooler com.android.quicksearchbox com.android.smspush com.android.contacts
    com.android.dreams.basic com.android.dreams.phototable
    com.android.galaxy4 com.android.magicsmoke com.android.musicvis com.android.noisefield
    com.android.phasebeam com.android.wallpaper com.android.wallpaper.holospiral
    com.android.wallpaper.livepicker
    com.svox.pico jp.co.omronsoft.openwnn
)

SMARTTUBE_URL=https://github.com/yuliskov/SmartTube/releases/download/latest/smarttube_stable.apk

root() { adb shell "su -c '$*'" | tr -d '\r'; }

if [ "${1:-}" = "--undo" ]; then
    for p in "${DISABLE[@]}"; do root "pm enable $p" >/dev/null; done
    echo "everything disabled has been re-enabled"
    exit 0
fi

adb get-state >/dev/null

echo ">> removing third-party apps"
for p in $(adb shell pm list packages -3 | tr -d '\r' | sed 's/^package:\(.*=\)\?//'); do
    case $p in org.smarttube.*) continue ;; esac
    printf '   %-40s ' "$p"; root "pm uninstall $p"
done

echo ">> disabling system bloat"
for p in "${DISABLE[@]}"; do
    printf '   %-45s ' "$p"; root "pm disable $p" | grep -o -E 'disabled|Unknown.*' || echo "?"
done

echo ">> installing SmartTube"
mkdir -p "$HERE/cache"
[ -f "$HERE/cache/smarttube.apk" ] || curl -fL -o "$HERE/cache/smarttube.apk" "$SMARTTUBE_URL"
adb install -r "$HERE/cache/smarttube.apk"

echo ">> settings"
# Time and time zone (tzdata in 4.4 only knows the old name Kiev)
root "setprop persist.sys.timezone Europe/Kiev"
root "settings put global auto_time 1"
# Animations twice as fast: the UI feels snappier
for k in window_animation_scale transition_animation_scale animator_duration_scale; do
    root "settings put global $k 0.5"
done

echo ">> done; reboot the box: adb reboot"
