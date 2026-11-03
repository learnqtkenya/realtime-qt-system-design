#!/usr/bin/env bash
# Virtual X desktop for watching and driving the apps without touching the real session.
# Rendering here is software (llvmpipe): use it to look and click, never to measure.
#
#   scripts/vdesk.sh start                 start Xvfb on $VDISPLAY
#   scripts/vdesk.sh run <app> [args...]   launch an app on it (background)
#   scripts/vdesk.sh shot <file.png>       screenshot the whole desktop
#   scripts/vdesk.sh click <x> <y>         left click
#   scripts/vdesk.sh key <keysym>          send a key, e.g. Escape
#   scripts/vdesk.sh record <file.mp4> <s> record the desktop for <s> seconds
#   scripts/vdesk.sh kill                  stop the apps started with run
#   scripts/vdesk.sh stop                  stop apps and Xvfb
set -euo pipefail

VDISPLAY=${VDISPLAY:-:99}
SIZE=${VSIZE:-1600x900}
STATE=${XDG_RUNTIME_DIR:-/tmp}/qtqmlpro-vdesk
mkdir -p "$STATE"

export DISPLAY=$VDISPLAY

case "${1:-}" in
start)
    if [[ -f $STATE/xvfb.pid ]] && kill -0 "$(cat "$STATE/xvfb.pid")" 2>/dev/null; then
        echo "already running on $VDISPLAY"; exit 0
    fi
    Xvfb "$VDISPLAY" -screen 0 "${SIZE}x24" -nolisten tcp >"$STATE/xvfb.log" 2>&1 &
    echo $! >"$STATE/xvfb.pid"
    for _ in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
    echo "Xvfb on $VDISPLAY ($SIZE)"
    ;;
run)
    shift
    env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb "$@" >"$STATE/app.log" 2>&1 &
    echo $! >>"$STATE/apps.pid"
    echo "pid $! (log: $STATE/app.log)"
    ;;
shot)
    import -window root "${2:?file}"
    echo "${2}"
    ;;
click)
    xdotool mousemove "${2:?x}" "${3:?y}" click 1
    ;;
key)
    xdotool key "${2:?keysym}"
    ;;
record)
    ffmpeg -loglevel error -y -f x11grab -video_size "$SIZE" -framerate 30 -i "$VDISPLAY" \
        -t "${3:-10}" -pix_fmt yuv420p "${2:?file}"
    echo "${2}"
    ;;
kill)
    [[ -f $STATE/apps.pid ]] && xargs -r kill <"$STATE/apps.pid" 2>/dev/null || true
    rm -f "$STATE/apps.pid"
    ;;
stop)
    "$0" kill
    [[ -f $STATE/xvfb.pid ]] && kill "$(cat "$STATE/xvfb.pid")" 2>/dev/null || true
    rm -f "$STATE/xvfb.pid"
    ;;
*)
    sed -n '2,13p' "$0"; exit 2 ;;
esac
