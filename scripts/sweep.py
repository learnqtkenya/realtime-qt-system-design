#!/usr/bin/env python3
"""Load sweep for a module app. Writes results/<module>/raw/*.json and results/<module>/summary.md.

    python3 scripts/sweep.py m02 [--series A B] [--summarise-only]
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench  # noqa: E402

MODULES = {
    "m01": ("experiments/01-first-implementation/m01_first",
            ["convert_points_ms", "lidar_signal_ms", "canvas_paint_ms",
             "convert_tracks_ms", "tracks_rebuild_ms", "event_reset_ms"]),
    "m02": ("experiments/02-boundaries/m02_boundaries",
            ["paint_ms", "tracks_rebuild_ms", "event_reset_ms"]),
    "m03": ("experiments/03-pipeline/m03_pipeline",
            ["raster_ms", "upload_ms", "tracks_rebuild_ms", "event_reset_ms"]),
    "m04": ("experiments/04-gpu/m04_gpu",
            ["sg_copy_ms", "rhi_prepare_ms", "q3d_setdata_ms", "raster_ms",
             "tracks_rebuild_ms", "event_reset_ms"]),
}

MODULES["step-01"] = ("app/viewer",
                      ["convert_points_ms", "lidar_signal_ms", "canvas_paint_ms", "paint_ms", "raster_ms",
                       "sg_copy_ms", "rhi_prepare_ms", "q3d_setdata_ms", "tracks_rebuild_ms",
                       "event_reset_ms", "switch_to_content_ms", "workload_apply_ms"])

# GPU timestamps on for every run of these modules, including baselines run alongside them.
MODULE_ENV = {"m04": {"QSG_RHI_PROFILE": "1"}, "step-01": {"QSG_RHI_PROFILE": "1"}}

RATES = {"step-01": ["submitted", "uploaded", "raster_ms", "dropped"],
         "m03": ["submitted", "dropped", "raster_ms", "upload_ms"],
         "m04": ["submitted", "uploaded", "raster_ms"]}

PEAK = ["--points", 300_000, "--rate", 20, "--objects", 50, "--events", 50]
M03 = "experiments/03-pipeline/m03_pipeline"
NOMINAL = ["--points", 150_000, "--objects", 50, "--events", 50]
GPU_VARIANTS = ("sg", "rhi", "quick3d")


def _m04_series():
    s = {"control": [("control-nosim", ["--no-sim"], 15)]}
    for v in GPU_VARIANTS:
        s[f"A-{v}"] = [(f"A-{v}-pts{p // 1000}k", ["--variant", v, "--points", p, "--objects", 50, "--events", 50], 15)
                       for p in (1_000, 5_000, 10_000, 25_000, 50_000, 100_000, 150_000)]
        s[f"peak-{v}"] = [(f"peak-{v}", ["--variant", v, *PEAK], 20)]
        s[f"stress-{v}"] = [(f"stress-{v}-pts{p // 1000}k", ["--variant", v, "--points", p, "--rate", 20,
                                                             "--objects", 50, "--events", 50], 20)
                            for p in (600_000, 1_000_000)]
        s[f"vulkan-{v}"] = [(f"vulkan-{v}-nominal", ["--variant", v, "--api", "vulkan", *NOMINAL], 20),
                            (f"vulkan-{v}-peak", ["--variant", v, "--api", "vulkan", *PEAK], 20)]
        s[f"repeat-{v}"] = [(f"repeat-{v}-{load}-r{r}", ["--variant", v, *args], 20)
                            for r in (2, 3) for load, args in (("nominal", NOMINAL), ("peak", PEAK))]
    s["baseline-raster"] = [(f"raster-{load}-r{r}", args, 20, M03)
                            for r in (1, 2, 3) for load, args in (("nominal", NOMINAL), ("peak", PEAK))]
    # Interleaved A/B so machine drift hits both variants equally.
    configs = {"peak": PEAK,
               "pts600k": ["--points", 600_000, "--rate", 20, "--objects", 50, "--events", 50],
               "pts1000k": ["--points", 1_000_000, "--rate", 20, "--objects", 50, "--events", 50],
               "vulkan-peak": ["--api", "vulkan", *PEAK]}
    s["ring-ab"] = [(f"ab-{c}-{v}-r{r}", ["--variant", v, *args], 20)
                    for r in (1, 2, 3) for c, args in configs.items() for v in ("rhi", "rhi-ring")]
    s["baseline-raster"].append(("raster-stress-pts600k", ["--points", 600_000, "--rate", 20, "--objects", 50,
                                                           "--events", 50], 20, M03))
    return s


RENDERER_PPS = {"canvas": 0.5e6, "painted": 6e6}  # mirrors Lab's caps: points per second


def capped(renderer, points, rate):
    pps = RENDERER_PPS.get(renderer)
    return points if pps is None else min(points, int(pps / (4 * rate) // 1000 * 1000))
RENDERERS = ("canvas", "painted", "worker", "sg", "rhi", "quick3d")


def _step01_series():
    def load(r, points, rate):
        return ["--variant", r, "--points", capped(r, points, rate), "--rate", rate,
                "--objects", 50, "--events", 50]
    s = {"control": [("control-nosim", ["--no-sim"], 15)]}
    # Interleaved so machine drift is spread across renderers.
    s["nominal"] = [(f"nominal-{r}-r{n}", load(r, 150_000, 10), 20) for n in (1, 2, 3) for r in RENDERERS]
    s["peak"] = [(f"peak-{r}", load(r, 300_000, 20), 20) for r in RENDERERS]
    s["charts"] = [(f"charts-{state}-{r}-r{n}", [*load(r, 150_000, 10), "--set", f"charts={int(state == 'on')}"], 20)
                   for n in (1, 2, 3) for r in ("rhi", "worker") for state in ("on", "off")]
    s["switch"] = [(f"switch-soak-r{n}", ["--points", 20_000, "--set", "cycle=1.5"], 60) for n in (1, 2)]
    s["soak"] = [("soak-all-300s", ["--points", 20_000, "--set", "cycle=1.5"], 300),
                 ("soak-no-canvas-300s", ["--points", 20_000, "--set", "cycle=1.5",
                                          "--set", "cycleSet=painted,worker,sg,rhi,quick3d"], 300)]
    s["recover"] = [(f"recover-canvas-to-rhi-r{n}", [*load("canvas", 150_000, 10), "--set", "switchTo=rhi@10"], 25)
                    for n in (1, 2)]
    s["loadchange"] = [(f"loadchange-{r}", ["--variant", r, "--set", "loadCycle=3"], 30) for r in ("rhi", "worker")]
    return s


def _step02_series():
    nominal = ["--points", 150_000, "--rate", 10, "--objects", 50, "--events", 50]
    peak = ["--points", 300_000, "--rate", 20, "--objects", 50, "--events", 50]
    stress = ["--points", 600_000, "--rate", 20, "--objects", 50, "--events", 50]
    orbit = ["--set", "view=orbit"]
    s = {"control": [("control-nosim", ["--no-sim"], 15)]}
    s["view"] = [(f"{v}-nominal-r{n}", [*nominal, *(orbit if v == "orbit" else ["--variant", "rhi"])], 20)
                 for n in (1, 2, 3) for v in ("orbit", "top-rhi")]
    s["view"].append(("orbit-peak", [*peak, *orbit], 20))
    s["lod"] = [(f"lod-{state}-far-r{n}", [*stress, *orbit, "--set", "zoom=300", "--set", f"lod={int(state == 'on')}"], 20)
                for n in (1, 2, 3) for state in ("on", "off")]
    s["input"] = [(f"input-{name}-r{n}", [*nominal, *args], 20) for n in (1, 2, 3)
                  for name, args in (("orbit-drag", [*orbit, "--set", "robot=orbit"]),
                                     ("top-rhi-click", ["--variant", "rhi", "--set", "robot=click"]),
                                     ("top-painted-click", ["--variant", "painted", "--set", "robot=click"]))]
    s["pick"] = [(f"pick-{o}-objects", ["--points", 150_000, "--objects", o, "--events", 50, *orbit,
                                        "--set", "robot=click3d"], 20) for o in (50, 200, 800, 2000)]
    return s


def _step03_series():
    base = ["--points", 150_000, "--rate", 10, "--objects", 50, "--events", 50, "--set", "view=orbit"]
    def cams(path, sync, hw):
        return [*base, "--set", "cameras=1", "--set", f"cameraPath={path}", "--set", f"cameraSync={sync}",
                "--set", f"hwdecode={hw}"]
    configs = {"none": [*base, "--set", "cameras=0"],
               "forward-latest-hw": cams("forward", "latest", 1),
               "forward-latest-sw": cams("forward", "latest", 0),
               "image-latest-hw": cams("image", "latest", 1),
               "forward-aligned-hw": cams("forward", "aligned", 1)}
    s = {"cameras": [(f"{c}-r{n}", args, 20) for n in (1, 2, 3) for c, args in configs.items()]}
    s["extra"] = [("image-latest-sw", cams("image", "latest", 0), 20),
                  ("drift-forward-latest-hw-120s", cams("forward", "latest", 1), 120)]
    return s


MODULES["step-02"] = ("app/viewer", MODULES["step-01"][1] + ["pick_index_ms", "pick_quick3d_ms", "index_build_ms"])
MODULE_ENV["step-02"] = {"QSG_RHI_PROFILE": "1"}
RATES["step-02"] = ["submitted", "uploaded"]
MODULES["step-03"] = ("app/viewer", MODULES["step-02"][1] + ["camera_toimage_ms", "camera_arrival_ms"])
MODULE_ENV["step-03"] = {"QSG_RHI_PROFILE": "1"}
RATES["step-03"] = ["camera_decoded", "camera_presented"]
MODULE_SERIES = {"m04": _m04_series(), "step-01": _step01_series(), "step-02": _step02_series(),
                 "step-03": _step03_series()}

EXTRA_SERIES = {
    "m03": {
        "P": [
            ("P-block-q4-w1", [*PEAK, "--workers", 1, "--policy", "block", "--queue", 4], 20),
            ("P-dropnewest-q4-w1", [*PEAK, "--workers", 1, "--policy", "drop-newest", "--queue", 4], 20),
            ("P-dropoldest-q4-w1", [*PEAK, "--workers", 1, "--policy", "drop-oldest", "--queue", 4], 20),
            ("P-dropoldest-q1-w1", [*PEAK, "--workers", 1, "--policy", "drop-oldest", "--queue", 1], 20),
            ("P-dropoldest-q1-w4", [*PEAK, "--workers", 4, "--policy", "drop-oldest", "--queue", 1], 20),
        ],
    },
}

SERIES = {
    "control": [("control-nosim", ["--no-sim"], 15)],
    "A": [(f"A-pts{p // 1000}k", ["--points", p, "--objects", 50, "--events", 50], 15)
          for p in (1_000, 5_000, 10_000, 25_000, 50_000, 100_000, 150_000)],
    "B": [(f"B-obj{o}", ["--points", 1_000, "--objects", o, "--events", 50], 15)
          for o in (50, 200, 400, 800)],
    "C": [(f"C-ev{e}", ["--points", 1_000, "--objects", 50, "--events", e], 30)
          for e in (50, 500, 2000)],
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module", choices=MODULES)
    ap.add_argument("--series", nargs="*")
    ap.add_argument("--summarise-only", action="store_true")
    ap.add_argument("--only", nargs="*", help="run only these labels")
    a = ap.parse_args()

    app_rel, stages = MODULES[a.module]
    series = MODULE_SERIES.get(a.module) or {**SERIES, **EXTRA_SERIES.get(a.module, {})}
    env = MODULE_ENV.get(a.module)
    rates = RATES.get(a.module, [])
    a.series = a.series or list(series)
    app = bench.BUILD / app_rel
    out = bench.ROOT / "results" / a.module
    raw = out / "raw"
    raw.mkdir(parents=True, exist_ok=True)

    if not a.summarise_only:
        for name in a.series:
            for label, args, duration, *override in series[name]:
                if a.only and label not in a.only:
                    continue
                target = bench.BUILD / override[0] if override else app
                bench.run(target, label, raw, args, duration=duration, env=env)

    sections = [f"# {a.module} sweep summary\n\nEnvironment: {json.dumps(bench.env_note())}\n",
                "Stage columns are milliseconds per second of wall time on the thread that "
                "runs the stage (1000 = that thread did nothing else).\n"]
    for name, runs in series.items():
        rows = [bench.row(json.loads((raw / f"{label}.json").read_text()), stages, rates)
                for label, *_ in runs if (raw / f"{label}.json").exists()]
        if rows:
            sections.append(f"## Series {name}\n\n{bench.markdown(rows)}\n")
    (out / "summary.md").write_text("\n".join(sections))
    print(f"wrote {out / 'summary.md'}")


if __name__ == "__main__":
    main()
