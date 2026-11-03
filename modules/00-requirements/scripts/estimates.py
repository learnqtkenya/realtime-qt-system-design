#!/usr/bin/env python3
"""Module 0 capacity estimates. Run before measuring; output goes to results/m00/estimates.md."""

MB = 1e6
GB = 1e9

BYTES_PER_POINT = 12  # x, y, z as float32

workloads = {
    "nominal": dict(sensors=4, points=150_000, hz=10),
    "peak": dict(sensors=4, points=300_000, hz=20),
}

print("# Module 0: capacity estimates (written before measuring)\n")

print("## A. Lidar data volume\n")
print("| Case | Sensors | Points/frame | Hz | Points/s | Bytes/frame | Bandwidth |")
print("|---|---|---|---|---|---|---|")
for name, w in workloads.items():
    pps = w["sensors"] * w["points"] * w["hz"]
    frame_bytes = w["points"] * BYTES_PER_POINT
    bw = pps * BYTES_PER_POINT
    print(
        f"| {name} | {w['sensors']} | {w['points']:,} | {w['hz']} | {pps/1e6:.1f} M "
        f"| {frame_bytes/MB:.1f} MB | {bw/MB:.0f} MB/s |"
    )

print(
    "\n**Correction to PLAN.md.** The plan said \"about 1-5M points/s\". With 4 sensors "
    "of 100k-300k points at 10-20 Hz the real range is 4M-24M points/s. "
    "Nominal is now 6M points/s (72 MB/s) and peak is 24M points/s (288 MB/s).\n"
)

print("## B. Video volume (needed from Module 5)\n")
W, H, FPS, STREAMS = 1920, 1080, 30, 6
nv12 = W * H * 1.5
rgba = W * H * 4
print("| Format | Bytes/frame | One stream | 6 streams |")
print("|---|---|---|---|")
for name, b in (("NV12 (decoder output)", nv12), ("RGBA8 (after conversion)", rgba)):
    print(f"| {name} | {b/MB:.2f} MB | {b*FPS/MB:.0f} MB/s | {b*FPS*STREAMS/GB:.2f} GB/s |")

print("\nEvery extra copy of the 6-stream RGBA path costs the following, counting a read and a write:\n")
print("| Copies | Memory traffic |")
print("|---|---|")
for c in (1, 2, 3, 4):
    print(f"| {c} | {rgba*FPS*STREAMS*c*2/GB:.1f} GB/s |")

print("\n## C. Frame time budget at 60 fps (16.67 ms)\n")
print("Proposed allocation. Module 0 turns these into measured pass/fail limits.\n")
print("| Stage | Thread | Budget |")
print("|---|---|---|")
for s, t, b in (
    ("Input, bindings, animations, model updates", "GUI", "<= 4.0 ms"),
    ("Scene-graph sync (GUI thread blocked)", "GUI + render", "<= 1.5 ms"),
    ("Render recording and submit", "render", "<= 6.0 ms"),
    ("GPU execution", "GPU", "<= 12.0 ms (overlaps CPU work)"),
    ("Headroom for the compositor and jitter", "-", ">= 3.0 ms"),
):
    print(f"| {s} | {t} | {b} |")

print("\n## D. Predictions for the Module 0 measurements\n")
print(
    "These are guesses made before looking at any number. The README records "
    "the measured values next to them.\n"
)
print("| Quantity | Prediction | Reasoning |")
print("|---|---|---|")
for q, p, r in (
    (
        "Cloud generation cost",
        "20-60 ns per point",
        "One sin, one cos and a few RNG calls per point; scalar code, no SIMD",
    ),
    (
        "Single-thread memcpy bandwidth",
        "10-25 GB/s",
        "Laptop-class DDR5, one core; the memory controller limit is far higher",
    ),
    (
        "QVariantList of QVector3D, build cost",
        "30-80 ns per point",
        "One heap allocation per QVariant plus the append",
    ),
    (
        "Simulator delivery at the nominal workload",
        ">= 99% of requested frames, inter-arrival jitter < 5 ms",
        "Frames come from a pre-built pool, so emission is a pointer copy",
    ),
    (
        "Idle scene frame interval",
        "Equals the display refresh period, p99 within 1 ms of the median",
        "A trivial scene is vsync-bound",
    ),
    (
        "Event-loop lag in the idle scene",
        "p99 < 1 ms",
        "Nothing is running on the GUI thread",
    ),
):
    print(f"| {q} | {p} | {r} |")
