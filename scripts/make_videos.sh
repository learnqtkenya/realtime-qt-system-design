#!/usr/bin/env bash
# Synthetic camera videos: 1080p30 H.264 High, camera id and frame number burned in.
# Output: build/release/media/cam{0..3}.mp4 (not committed; regenerate any time).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${OUT:-build/release/media}
# Keyframe interval in frames: seek latency against file size (results/step-04/video-seek.md).
GOP=${GOP:-30}
FONT=$(fc-match -f "%{file}" "DejaVu Sans Mono")
SECONDS_LONG=${SECONDS_LONG:-30}
mkdir -p "$OUT"
for i in 0 1 2 3; do
    ffmpeg -loglevel error -y -f lavfi -i "testsrc2=size=1920x1080:rate=30" -t "$SECONDS_LONG" \
        -vf "hue=h=$((i * 90)),drawtext=fontfile=$FONT:text='cam $i  frame %{n}':x=60:y=60:fontsize=72:fontcolor=white:box=1:boxcolor=black@0.6" \
        -c:v libx264 -profile:v high -pix_fmt yuv420p -g "$GOP" -bf 0 "$OUT/cam$i.mp4"
done
ls -la "$OUT"
