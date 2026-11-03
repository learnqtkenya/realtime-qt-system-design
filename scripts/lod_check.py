#!/usr/bin/env python3
"""How different does the 3D view look with level of detail on? Writes results/step-02/lod.md."""

import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench  # noqa: E402
import parity  # noqa: E402

OUT = bench.ROOT / "results" / "step-02" / "lod"
VIEWER = bench.BUILD / "app" / "viewer"
BACKGROUND = (0x0B, 0x10, 0x20)
REGION = (0, 170, 1220, 900)


def grab(zoom, lod, points):
    path = OUT / f"zoom{zoom}-lod{lod}-pts{points // 1000}k.png"
    subprocess.run([str(VIEWER), "--still", "--points", str(points), "--rate", "20", "--objects", "0",
                    "--events", "0", "--set", "view=orbit", "--set", f"zoom={zoom}", "--set", f"lod={lod}",
                    "--grab", str(path), "--grab-after", "4"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60, check=True)
    return path


def lit(path):
    w, _, raw = parity.load(path)
    x0, y0, x1, y1 = REGION
    out = set()
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = (y * w + x) * 3
            d = sum((raw[i + k] - BACKGROUND[k]) ** 2 for k in range(3))
            if d > 40 ** 2:
                out.add((x, y))
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for zoom in (90, 300, 500):
        full = lit(grab(zoom, 0, 600_000))
        thin = lit(grab(zoom, 1, 600_000))
        stride = max(1, min(8, 2 ** int(__import__("math").log2(zoom / 60)))) if zoom >= 60 else 1
        rows.append({"camera distance m": zoom, "LOD stride": stride, "lit px, all points": len(full),
                     "lit px, LOD": len(thin), "lit ratio": len(thin) / max(1, len(full)),
                     "LOD px near a full px": parity.near(thin, full),
                     "full px near a LOD px": parity.near(full, thin)})
        print(rows[-1], flush=True)
    (OUT.parent / "lod.json").write_text(json.dumps(rows, indent=2))
    (OUT.parent / "lod.md").write_text(
        "# Step 2: what level of detail costs the picture\n\n"
        "Frozen scene, 4 × 600k points, no objects. Lit = differs from the background by more than 40 "
        "(RGB distance). Agreement allows 1 pixel of positional tolerance.\n\n" + bench.markdown(rows) + "\n")


if __name__ == "__main__":
    main()
