#!/usr/bin/env python3
"""Do the GPU point renderers draw the same picture as the CPU raster?

Every variant renders the same frozen scene (--still) and saves a screenshot
(--grab). Lit pixels are compared with Module 3's QPainter output, allowing
1 pixel of positional tolerance. Writes results/m04/parity.md and parity.json.
"""

import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench  # noqa: E402

OUT = bench.ROOT / "results" / "m04" / "parity"
SCENE = ["--still", "--points", "20000", "--objects", "50", "--events", "0", "--grab-after", "3"]
REFERENCE = ("raster", bench.BUILD / "modules/03-pipeline/m03_pipeline", [])
VARIANTS = [(v, bench.BUILD / "modules/04-gpu/m04_gpu", ["--variant", v]) for v in ("sg", "rhi", "rhi-ring", "quick3d")]
APIS = ("opengl", "vulkan")

# Point area only: left of the event log, below the HUD.
REGION = (0, 170, 1040, 800)
SENSOR_RGB = [(0x38, 0xBD, 0xF8), (0x34, 0xD3, 0x99), (0xFB, 0xBF, 0x24), (0xF4, 0x72, 0xB6)]
PASS = 0.98


def grab(name, app, args, api):
    path = OUT / f"{name}-{api}.png"
    path.unlink(missing_ok=True)
    proc = subprocess.run([str(app), *SCENE, *args, "--api", api, "--grab", str(path)],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
    if not path.exists():
        sys.exit(f"{name}/{api}: no screenshot (exit {proc.returncode})\n{proc.stdout}")
    return path, [l for l in proc.stdout.splitlines() if l.strip()]


def load(path):
    w, h = map(int, subprocess.check_output(["identify", "-format", "%w %h", str(path)]).split())
    raw = subprocess.check_output(["convert", str(path), "-depth", "8", "rgb:-"])
    return w, h, raw


def classify(img):
    """Pixel sets per sensor colour, plus near-white pixels (track boxes)."""
    w, _, raw = img
    x0, y0, x1, y1 = REGION
    sensors = [set() for _ in SENSOR_RGB]
    white = set()
    colour_error = []
    for y in range(y0, y1):
        row = y * w * 3
        for x in range(x0, x1):
            i = row + x * 3
            r, g, b = raw[i], raw[i + 1], raw[i + 2]
            if r > 230 and g > 230 and b > 230:
                white.add((x, y))
                continue
            best, dist = -1, 1e9
            for k, (sr, sg, sb) in enumerate(SENSOR_RGB):
                d = (r - sr) ** 2 + (g - sg) ** 2 + (b - sb) ** 2
                if d < dist:
                    best, dist = k, d
            if dist < 45 ** 2:
                sensors[best].add((x, y))
                colour_error.append(dist ** 0.5)
    lit = set().union(*sensors)
    mean_err = sum(colour_error) / len(colour_error) if colour_error else 0.0
    return sensors, lit, white, mean_err


def near(a, b):
    """Fraction of a within 1 pixel (8-neighbourhood) of b."""
    if not a:
        return 1.0
    hit = sum(1 for (x, y) in a if any((x + dx, y + dy) in b for dx in (-1, 0, 1) for dy in (-1, 0, 1)))
    return hit / len(a)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows, failures = [], []
    for api in APIS:
        ref_path, ref_err = grab(REFERENCE[0], REFERENCE[1], REFERENCE[2], api)
        _, ref_lit, ref_white, _ = classify(load(ref_path))
        for name, app, args in VARIANTS:
            path, err = grab(name, app, args, api)
            _, lit, white, colour_err = classify(load(path))
            row = {
                "variant": name,
                "api": api,
                "ref lit px": len(ref_lit),
                "lit px": len(lit),
                "lit ratio": len(lit) / max(1, len(ref_lit)),
                "precision": near(lit, ref_lit),
                "recall": near(ref_lit, lit),
                "white ratio": len(white) / max(1, len(ref_white)),
                "colour error": colour_err,
                "stderr": len(err) + len(ref_err),
            }
            row["pass"] = (row["precision"] >= PASS and row["recall"] >= PASS
                           and 0.9 <= row["white ratio"] <= 1.1 and row["stderr"] == 0)
            if not row["pass"]:
                failures.append(f"{name}/{api}")
            rows.append(row)
            print({k: (round(v, 3) if isinstance(v, float) else v) for k, v in row.items()}, flush=True)

    (OUT.parent / "parity.json").write_text(json.dumps(rows, indent=2))
    md = ["# Module 4: pixel parity against the CPU raster (Module 3)\n",
          f"Scene: `{' '.join(SCENE)}`. Region x {REGION[0]}-{REGION[2]}, y {REGION[1]}-{REGION[3]}. "
          f"Precision and recall allow 1 pixel of positional tolerance. Pass: both ≥ {PASS}, "
          "white (track box) pixel count within 10%, no stderr output.\n",
          bench.markdown(rows)]
    (OUT.parent / "parity.md").write_text("\n".join(md) + "\n")
    print("FAILED: " + ", ".join(failures) if failures else "all variants pass")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
