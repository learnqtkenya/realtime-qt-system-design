"""Shared helpers: run a module app in benchmark mode and summarise its reports."""

import json
import os
import subprocess
import time
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "release"


def run(app, label, out_dir, args, warmup=3, duration=15, timeout_pad=30, retries=2, env=None):
    for attempt in range(retries + 1):
        report = _run_once(app, label, out_dir, args, warmup, duration, timeout_pad, env)
        if not report:
            print(f"[{label}] no report; retrying", flush=True)
            continue
        if report.get("aborted"):
            print(f"[{label}] aborted: {report['aborted']}; retrying", flush=True)
            continue
        if not report.get("suspectExternalPause"):
            break
        print(f"[{label}] presentation paused while the GUI thread was idle; retrying", flush=True)
    if report:
        report["attempts"] = attempt + 1
        (Path(out_dir) / f"{label}.json").write_text(json.dumps(report, indent=2))
    return report


def _run_once(app, label, out_dir, args, warmup, duration, timeout_pad, env=None):
    out = Path(out_dir) / f"{label}.json"
    cmd = [str(app), "--bench", "--warmup", str(warmup), "--duration", str(duration),
           "--label", label, "--out", str(out), *map(str, args)]
    print(f"{time.strftime('%H:%M:%S')} [{label}] {' '.join(cmd[1:])}", flush=True)
    try:
        proc = subprocess.run(cmd, timeout=warmup + duration + timeout_pad,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              env={**os.environ, **(env or {})})
        status = proc.returncode
    except subprocess.TimeoutExpired:
        status = "timeout"
    if not out.exists():
        print(f"[{label}] no report (status {status})", file=sys.stderr)
        return None
    report = json.loads(out.read_text())
    report["exitStatus"] = status
    lines = [] if status == "timeout" else [l for l in proc.stdout.splitlines() if l.strip()]
    report["stderrLines"] = lines[:50]
    if lines:
        print(f"[{label}] {len(lines)} line(s) on stderr, first: {lines[0][:160]}", flush=True)
    out.write_text(json.dumps(report, indent=2))
    return report


def stage_load(report, name):
    """GUI milliseconds spent in a stage per second of wall time."""
    s = report.get("stages", {}).get(name)
    if not s or report["measuredSeconds"] <= 0:
        return 0.0
    return s["n"] * s["mean"] / report["measuredSeconds"]


def rate(report, name):
    s = report.get("stages", {}).get(name)
    return s["n"] / report["measuredSeconds"] if s and report["measuredSeconds"] > 0 else 0.0


def row(report, stages, rates=()):
    e = report["extras"]
    lat = report["latencyMs"]
    backlog = report.get("stages", {}).get("backlog_frames", {})
    r = {
        "label": report["label"],
        "pts/frame": e.get("pointsPerFrame"),
        "objects": e.get("objects"),
        "ev/s": e.get("eventsPerSecond"),
        "fps": report["fps"],
        "frame p50": report["frameMs"]["p50"],
        "frame p99": report["frameMs"]["p99"],
        "frame max": report["frameMs"]["max"],
        ">20ms": report["framesOver"]["gt20ms"],
        "lag p99": report["eventLoopLagMs"]["p99"],
        "lag max": report["eventLoopLagMs"]["max"],
        "gui cpu%": report["guiCpuPercent"]["mean"],
        "lat p50": lat["p50"] if lat["n"] else None,
        "lat max": lat["max"] if lat["n"] else None,
        "backlog max": backlog.get("max"),
        "rss MB/min": report["rssGrowthMbPerMinute"],
        "stalled": report["stalled"],
        "exit": report.get("exitStatus"),
        "ext pause": report.get("suspectExternalPause"),
        "aborted": bool(report.get("aborted")),
        "stderr": len(report.get("stderrLines", [])),
        "gpu p50": report.get("gpuMs", {}).get("p50") or None,
        "gpu p99": report.get("gpuMs", {}).get("p99") or None,
        "proc cpu%": report["processCpuPercent"]["mean"],
        "rss max": report.get("rssMb", {}).get("max"),
        "cam lat p50": report.get("cameraLatencyMs", {}).get("p50") or None,
        "|skew| p50": report.get("cameraAbsSkewMs", {}).get("p50") or None,
        "|skew| p99": report.get("cameraAbsSkewMs", {}).get("p99") or None,
        # Same work in every run (50-object track rebuild): a slower value means a slower machine.
        "canary ms": report.get("stages", {}).get("tracks_rebuild_ms", {}).get("p50"),
    }
    for s in stages:
        r[f"{s} ms/s"] = stage_load(report, s)
    for s in rates:
        r[f"{s.removesuffix('_ms')}/s"] = rate(report, s)
    return r


def fmt(v):
    if v is None:
        return "-"
    if isinstance(v, bool):
        return "yes" if v else "no"
    if isinstance(v, float):
        return f"{v:,.1f}" if abs(v) < 1e5 else f"{v:,.0f}"
    if isinstance(v, int):
        return f"{v:,}"
    return str(v)


def markdown(rows):
    if not rows:
        return ""
    keys = list(rows[0].keys())
    lines = ["| " + " | ".join(keys) + " |", "|" + "---|" * len(keys)]
    for r in rows:
        lines.append("| " + " | ".join(fmt(r.get(k)) for k in keys) + " |")
    return "\n".join(lines)


def env_note():
    gov = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
    return {
        "governor": gov.read_text().strip() if gov.exists() else "unknown",
        "session": os.environ.get("XDG_SESSION_TYPE", "unknown"),
    }
