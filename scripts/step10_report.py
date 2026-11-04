#!/usr/bin/env python3
"""Step 10 tables from results/step-10/raw -> results/step-10/analysis.md (medians of repeats)."""

import json
import re
import statistics as st
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "results/step-10/raw"


def runs(prefix):
    return [json.loads(p.read_text()) for p in sorted(RAW.glob(f"{prefix}-r*.json")) if re.fullmatch(rf"{prefix}-r\d+", p.stem)]


def fmt(v, d=1):
    return "–" if v is None else f"{v:,.{d}f}"


def table(header, rows):
    return "\n".join(["| " + " | ".join(header) + " |", "|" + "---|" * len(header)] + ["| " + " | ".join(r) + " |" for r in rows])


def row(title, rs):
    if not rs:
        return None
    g = lambda f: st.median([f(r) for r in rs])  # noqa: E731
    rate = lambda r, k: r["stages"].get(k, {}).get("n", 0) / r["measuredSeconds"]  # noqa: E731
    cam = [r["cameraLatencyMs"]["p50"] for r in rs if r["cameraLatencyMs"]["n"]]
    return [title, str(len(rs)), fmt(g(lambda r: r["frameMs"]["p99"])), fmt(g(lambda r: r["eventLoopLagMs"]["p99"]), 2),
            str(sum(r["framesOver"]["gt20ms"] for r in rs)), fmt(g(lambda r: r["latencyMs"]["p50"])),
            fmt(g(lambda r: r["latencyMs"]["p99"])), f"{g(lambda r: rate(r, 'uploaded')):.0f} of {g(lambda r: rate(r, 'submitted')):.0f}",
            fmt(st.median(cam)) if cam else "–", fmt(g(lambda r: r["processCpuPercent"]["mean"]), 0),
            fmt(g(lambda r: r["gpuMs"]["mean"]), 2), fmt(g(lambda r: r["rssMb"]["max"]), 0)]


HEADER = ["Run", "n", "Frame p99", "GUI lag p99", "Frames > 20 ms", "Latency p50", "Latency p99", "Lidar frames/s shown",
          "Camera latency p50", "CPU %", "GPU ms", "RSS MB"]


def main():
    out = ["# Step 10 analysis\n", "Medians of repeats; 20 s each after 3 s. *CPU %* is of one core.\n"]
    rows = [row("8 × 150k @ 10 Hz" if k == 8 else "4 × 150k @ 10 Hz", runs(f"lidars-{k}")) for k in (4, 8)]
    rows += [row(f"{k} × 150k @ 10 Hz, recording", runs(f"lidars-{k}-record")) for k in (4, 8)]
    out.append("## A. Eight lidars (nominal otherwise, cameras off)\n\n" + table(HEADER, [r for r in rows if r]) + "\n")
    rec = []
    for k in (4, 8):
        rs = runs(f"lidars-{k}-record")
        if rs:
            s = rs[0]["stages"]
            rec.append([str(k), fmt(s["record_lidar_encode_ms"]["n"] / rs[0]["measuredSeconds"], 0),
                        fmt(s["record_lidar_encode_ms"]["p50"], 2), fmt(s["record_chunk_write_ms"]["p50"], 2)])
    if rec:
        out.append("Recording (int16 planar + zstd 1): files of 504 MB (4 lidars) and 1,004 MB (8 lidars) for the same "
                   "23 s, i.e. 21.9 and 43.6 MB/s.\n\n" + table(["Lidars", "Frames encoded/s", "Encode ms p50", "Chunk write ms p50"], rec) + "\n")
    for suffix, title in (("-asbuilt", "as built"), ("", "client sized for eight sensors")):
        p = RAW / f"wire-lidars{suffix}.json"
        if p.exists():
            w = json.loads(p.read_text())
            out.append(f"Second viewer over the wire ({title}):\n\n" + table(
                ["Lidars on the server", "Received MB/s", "Lidar frames/s shown by the client", "Client latency p50 / p99"],
                [[k, fmt(v["clientMbps"]), fmt(v["clientLidarPerS"], 0),
                  f"{fmt(v['clientLatencyP50'])} / {fmt(v['clientLatencyP99'])}"] for k, v in w.items()]) + "\n")
    c = [row("nominal, 16 cores", runs("cores-16-cam0")), row("nominal, 2 cores", runs("cores-2-cam0")),
         row("nominal + cameras, 16 cores", runs("cores-16-cam1")), row("nominal + cameras, 2 cores", runs("cores-2-cam1")),
         row("nominal + cameras, 1 core", runs("cores-1-cam1")), row("peak + cameras, 16 cores", runs("peak-16")),
         row("peak + cameras, 2 cores", runs("peak-2")), row("peak + cameras, 1 core", runs("peak-1"))]
    out.append("## C. A smaller machine: cores (`taskset`)\n\nNominal: 4 × 150k @ 10 Hz, 200 objects, 200 events/s. "
               "Peak: 4 × 300k @ 20 Hz, 800 objects, 2,000 events/s. Cameras: four 1080p30 (VA-API).\n\n"
               + table(HEADER, [r for r in c if r]) + "\n")
    mem = []
    for label in ("mem-1g-asbuilt", "mem-1g-adaptive", "mem-1g-adaptive-cameras"):
        p = RAW / f"{label}.json"
        if p.exists():
            m = json.loads(p.read_text())
            last = m["series"][-1] if m["series"] else {}
            mem.append([label, "cameras" if "cameras" not in " ".join(m["args"]) else "no cameras",
                        "**killed** after %.0f s" % m["diedAfterS"] if not m["survived"] else "ran the full %.0f s" % last.get("t", 0),
                        fmt(last.get("rssMb"), 0), fmt(last.get("historyMb"), 0), fmt(last.get("historySeconds"), 1)])
    if mem:
        out.append("## C. A smaller machine: memory (a 1 GB cgroup limit, no swap)\n\n" + table(
            ["Run", "Cameras", "Outcome", "RSS MB (last)", "History MB", "History s"], mem) + "\n")
    compat = ROOT / "results/step-10/compat.md"
    if compat.exists():
        out.append("## B. Notes across versions\n\nSee [`compat.md`](compat.md).\n")
    (ROOT / "results/step-10/analysis.md").write_text("\n".join(out))
    print("\n".join(out))


if __name__ == "__main__":
    main()
