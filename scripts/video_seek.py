#!/usr/bin/env python3
"""Paused-seek latency against keyframe interval and parallel decoders (experiments/05-video-seek).

    python3 scripts/video_seek.py [--summarise-only]

Makes 30 s 1080p30 test videos with a keyframe every 30, 10 and 1 frames, runs 100 seeks per
decoder with 1 and 4 decoders, and writes results/step-04/video-seek.md.
"""

import csv
import os
import statistics as st
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MEDIA = ROOT / "build/release/media"
OUT = ROOT / "results/step-04/video-seek"
EXE = ROOT / "build/release/experiments/05-video-seek/e05_video_seek"
GOPS = (30, 10, 1)
DECODERS = (1, 4)


def make(gop):
    path = MEDIA / f"gop{gop}.mp4"
    if path.exists():
        return path
    font = subprocess.check_output(["fc-match", "-f", "%{file}", "DejaVu Sans Mono"], text=True)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=30",
                    "-t", "30", "-vf", f"drawtext=fontfile={font}:text='frame %{{n}}':x=60:y=60:fontsize=72:"
                    "fontcolor=white:box=1:boxcolor=black@0.6", "-c:v", "libx264", "-profile:v", "high",
                    "-pix_fmt", "yuv420p", "-g", str(gop), "-bf", "0", str(path)], check=True)
    return path


def q(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p * len(v)))]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "QT_LOGGING_RULES": "qt.multimedia.*.info=false"}
    if "--summarise-only" not in sys.argv:
        for g in GOPS:
            video = make(g)
            for d in DECODERS:
                with open(OUT / f"gop{g}-dec{d}.csv", "w") as out, open(OUT / f"gop{g}-dec{d}.err", "w") as err:
                    subprocess.run([str(EXE), str(video), str(d), "100", str(g)], stdout=out, stderr=err,
                                   env=env, timeout=150, check=True)
    lines = ["# Paused seek to the exact frame: keyframe interval and parallel decoders\n",
             "30 s 1080p30 H.264 (High, no B-frames), Qt Multimedia FFmpeg backend (VA-API), 100 random seeks per "
             "decoder, 50 ms between seeks. Latency: `setPosition()` to the frame arriving in the sink. "
             "Raw rows in `video-seek/*.csv` (stderr in `*.err`).\n",
             "| Keyframe every | File | Decoders | Seeks | p50 ms | p99 ms | max ms | Exact frame | "
             "Median by time since keyframe (0–250 / 250–500 / 500–750 / 750–1000 ms) |",
             "|---|---|---|---|---|---|---|---|---|"]
    for g in GOPS:
        size = (MEDIA / f"gop{g}.mp4").stat().st_size / 1e6
        for d in DECODERS:
            rows = list(csv.DictReader(open(OUT / f"gop{g}-dec{d}.csv")))
            lat = [float(r["latency_ms"]) for r in rows]
            exact = all(0 <= int(r["target_ms"]) - int(r["frame_ms"]) < 34 for r in rows)
            buckets = {}
            for r in rows:
                buckets.setdefault(int(r["since_keyframe_ms"]) // 250, []).append(float(r["latency_ms"]))
            by = " / ".join(f"{st.median(buckets[b]):.0f}" if b in buckets else "–" for b in range(4))
            stderr = (OUT / f"gop{g}-dec{d}.err").read_text().strip()
            lines.append(f"| {g} frames ({g / 30:.2f} s) | {size:.1f} MB | {d} | {len(rows)} | {q(lat, .5):.1f} | "
                         f"{q(lat, .99):.1f} | {max(lat):.1f} | {'yes' if exact else 'NO'} | {by} |"
                         + (f" stderr: {len(stderr.splitlines())} lines" if stderr else ""))
    (OUT.parent / "video-seek.md").write_text("\n".join(lines) + "\n")
    print((OUT.parent / "video-seek.md").read_text())


if __name__ == "__main__":
    main()
