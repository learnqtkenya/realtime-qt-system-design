#!/usr/bin/env python3
"""Step 8 tables from results/step-08/raw -> results/step-08/analysis.md (medians of repeats)."""

import json
import re
import statistics as st
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "results/step-08/raw"

LAYER = {"item": None, "js-gui": "speed-vectors", "qml-gui": "speed-vectors-qml", "js-worker": "speed-vectors",
         "qml-worker": "speed-vectors-qml", "py": "speed-vectors-py", "py-sandbox": "speed-vectors-py",
         "cpp": "speed-vectors-cpp"}
KIND = {"none": "no layer", "item": "item (viewer's engine)", "js-gui": "script, JS module, GUI thread",
        "qml-gui": "script, QML object, GUI thread", "js-worker": "script, JS module, own thread",
        "qml-worker": "script, QML object, own thread", "py": "process, Python",
        "py-sandbox": "process, Python, in bwrap", "cpp": "process, C++"}
BAD = {"spin-item": ("spin-item", "item, endless loop"), "spin-js-gui": ("spin-script", "script on GUI thread, endless loop"),
       "spin-js-worker": ("spin-script", "script on own thread, endless loop"),
       "slow-js-gui": ("slow-script", "script on GUI thread, 30 ms per update"),
       "slow-js-worker": ("slow-script", "script on own thread, 30 ms per update"),
       "crash": ("crash-process", "process, crashes"), "hang": ("hang-process", "process, stops answering"),
       "garbage": ("garbage-process", "process, writes garbage"),
       "flood-every": ("flood-process", "process floods; every overlay handed over, all decoded"),
       "flood-latest": ("flood-process", "process floods; newest decoded, hand-overs paced"),
       "flood-contract": ("flood-process", "process floods; one answer per input enforced")}


def load(prefix):
    runs = {}
    for p in sorted(RAW.glob(f"{prefix}-*.json")):
        m = re.match(rf"{prefix}-(.+)-r(\d+)$", p.stem)
        if m:
            r = json.loads(p.read_text())
            log = RAW / f"{p.stem}.layers.jsonl"
            r["_log"] = [json.loads(l) for l in log.read_text().splitlines()] if log.exists() else []
            runs.setdefault(m.group(1), []).append(r)
    return runs


def med(vals):
    vals = [v for v in vals if v is not None]
    return st.median(vals) if vals else None


def stage(r, name, key="p50"):
    s = r.get("stages", {}).get(name)
    return s.get(key) if s and s.get("n") else None


def fmt(v, d=1):
    return "–" if v is None else (v if isinstance(v, str) else f"{v:,.{d}f}")


def table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(out)


def cost_rows(runs, objects):
    rows = []
    for kind in KIND:
        rs = runs.get(f"{kind}-{objects}")
        if not rs:
            continue
        lid = LAYER.get(kind)
        g = lambda f: med([f(r) for r in rs])  # noqa: E731
        upd = g(lambda r: stage(r, f"layer_{lid}_update_ms")) if lid else None
        mar = g(lambda r: stage(r, f"layer_{lid}_marshal_ms")) if lid else None
        cpu = g(lambda r: stage(r, f"layer_{lid}_cpu_pct", "mean")) if lid else None
        work = (f"{fmt(mar, 2)} + {fmt(upd, 2)}" if upd is not None else
                (f"plugin CPU {fmt(cpu)}%" if cpu is not None else "–"))
        rows.append([KIND[kind], str(len(rs)),
                     fmt(g(lambda r: r["eventLoopLagMs"]["p99"]), 2),
                     fmt(g(lambda r: r["frameMs"]["p99"])),
                     fmt(g(lambda r: r["guiCpuPercent"]["mean"])),
                     fmt(g(lambda r: r["processCpuPercent"]["mean"])),
                     fmt(g(lambda r: r["rssMb"]["max"]), 0),
                     work,
                     fmt(g(lambda r: stage(r, f"layer_{lid}_latency_ms")), 2) if lid else "–",
                     fmt(g(lambda r: stage(r, f"layer_{lid}_latency_ms", "p99")), 2) if lid else "–",
                     fmt(g(lambda r: stage(r, "layers_sync_ms")), 3)])
    return rows


def timeline(log, layer, measured_from=0.0):
    events = [e for e in log if e.get("layer") == layer and "status" in e and e["status"] not in ("starting", "off")]
    return events


def bad_rows(runs):
    rows = []
    for key, (lid, title) in BAD.items():
        rs = runs.get(key)
        if not rs:
            continue
        g = lambda f: med([f(r) for r in rs])  # noqa: E731
        ev = timeline(rs[0]["_log"], lid)
        statuses = [e["status"] for e in ev]
        first_bad = next((e for e in ev if e["status"] not in ("ok",)), None)
        final = statuses[-1] if statuses else "–"
        restarts = sum(1 for s in statuses if s in ("crashed", "not responding", "error"))
        n_deliver = g(lambda r: (r["stages"].get(f"layer_{lid}_latency_ms", {}).get("n", 0)) / r["measuredSeconds"])
        rows.append([title,
                     "**stalled**" if any(r.get("stalled") for r in rs) else "no",
                     fmt(g(lambda r: r["eventLoopLagMs"]["p99"]), 2),
                     fmt(g(lambda r: r["eventLoopLagMs"]["max"])),
                     fmt(g(lambda r: r["guiCpuPercent"]["mean"])),
                     fmt(g(lambda r: r["processCpuPercent"]["mean"])),
                     fmt(n_deliver, 0),
                     (f"{first_bad['status']} at {first_bad['t']:.2f} s" if first_bad else "–"),
                     str(restarts), final,
                     (first_bad["detail"][:70] if first_bad else "")])
    return rows


def main():
    cost = load("cost")
    bad = load("bad")
    out = ["# Step 8 analysis\n",
           "Medians of the repeats (n). Nominal lidar (4 × 150k points at 10 Hz), 200 events/s, cameras off, "
           "RHI renderer. The layer is speed vectors in every case: one line per object.\n",
           "Columns: GUI lag and frame time p99 in ms; GUI-thread and whole-process CPU in % of one core; "
           "RSS max in MB; *work per update* is the layer's own cost on its thread "
           "(scripts: input conversion + `update()`, ms p50; processes: the plugin process's CPU); "
           "*latency* is tracks presented → overlay on the GUI thread (ms); *sync* is building the "
           "overlay nodes on the render thread (ms p50).\n"]
    header = ["Layer", "n", "GUI lag p99", "Frame p99", "GUI CPU", "Process CPU", "RSS", "Work per update",
              "Latency p50", "Latency p99", "Sync"]
    for objects in (200, 2000):
        out.append(f"## Cost, {objects} objects\n\n{table(header, cost_rows(cost, objects))}\n")
    out.append("## Misbehaving plugins\n\nOne layer each, 200 objects. The plugin misbehaves after 60 updates "
               "(about 6 s); the flood starts at once. *t* is seconds since the viewer started (measurement "
               "starts at 3 s). *Overlays/s* is how many overlays reached the GUI thread per second.\n")
    out.append(table(["Plugin", "Viewer stalled", "GUI lag p99", "GUI lag max", "GUI CPU", "Process CPU",
                      "Overlays/s", "First non-ok", "Restarts", "Final", "Why"], bad_rows(bad)) + "\n")
    out.append("### Status timelines (first run of each)\n")
    for key, (lid, title) in BAD.items():
        rs = bad.get(key)
        if not rs:
            continue
        ev = timeline(rs[0]["_log"], lid)
        out.append(f"**{title}**\n\n```\n" + "\n".join(f"{e['t']:7.3f} s  {e['status']:<15} {e['detail']}"
                                                         for e in ev[:14]) + "\n```\n")
    stats = {}
    for key, (lid, _) in BAD.items():
        for r in bad.get(key, []):
            for name in ("interrupted_after_ms", "restart_ms"):
                s = r["stages"].get(f"layer_{lid}_{name}")
                if s:
                    stats.setdefault(f"{key}: {name}", []).append(s["p50"])
    if stats:
        out.append("### Watchdog and restart times (p50 per run)\n\n" + table(
            ["Case", "Values (ms)"], [[k, ", ".join(f"{v:.0f}" for v in vs)] for k, vs in stats.items()]) + "\n")
    (ROOT / "results/step-08/analysis.md").write_text("\n".join(out))
    print("wrote results/step-08/analysis.md")


if __name__ == "__main__":
    main()
