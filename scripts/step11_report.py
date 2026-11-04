#!/usr/bin/env python3
"""Step 11 tables from results/step-11/raw -> results/step-11/analysis.md."""

import json
import re
import statistics as st
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "results/step-11/raw"


def load(name):
    p = RAW / f"{name}.json"
    return json.loads(p.read_text()) if p.exists() else None


def runs(prefix):
    return [json.loads(p.read_text()) for p in sorted(RAW.glob(f"{prefix}-r*.json")) if re.fullmatch(rf"{re.escape(prefix)}-r\d+", p.stem)]


def fmt(v, d=1):
    return "–" if v is None else f"{v:,.{d}f}"


def rng(values, d=1):
    values = [v for v in values if v is not None]
    if not values:
        return "–"
    lo, hi = min(values), max(values)
    return fmt(lo, d) if round(lo, d) == round(hi, d) else f"{fmt(lo, d)}–{fmt(hi, d)}"


def table(header, rows):
    return "\n".join(["| " + " | ".join(header) + " |", "|" + "---|" * len(header)] + ["| " + " | ".join(r) + " |" for r in rows])


def stage(r, key, field="mean"):
    v = r["stages"].get(key, {})
    return v.get(field) if v.get("n") else None


def shown(r):
    return r["stages"].get("uploaded", {}).get("n", 0) / r["measuredSeconds"]


def main_thread_row(title, rs):
    if not rs:
        return None
    return [title, str(len(rs)), rng([r["eventLoopLagMs"]["p99"] for r in rs], 2), rng([r["frameMs"]["p99"] for r in rs]),
            rng([r["syncMs"]["p99"] for r in rs], 2), rng([r["renderMs"]["mean"] for r in rs], 2),
            rng([stage(r, "overlay_labels_ms") for r in rs], 2), rng([stage(r, "tracks_rebuild_ms") for r in rs], 2),
            rng([r["latencyMs"]["p50"] for r in rs]), rng([shown(r) for r in rs]), rng([r["rssMb"]["max"] for r in rs], 0)]


def series_at(series, t):
    best = None
    for tt, v in series:
        if tt <= t:
            best = v
    return best


def behind_row(title, name):
    r = load(name)
    o = RAW / f"{name}-outside.json"
    if r is None or not o.exists():
        return None
    o = json.loads(o.read_text())["series"]
    lat = [v for _, v in r["latencySeries"] if v > 0]
    backlog = [s.get("backlogMb") for s in o if s.get("backlogMb") is not None]
    replaced = [s.get("replaced") for s in o if s.get("replaced") is not None]
    tabs = [max((v for k, v in s.get("firefoxRssMb", {}).items() if k.endswith(" tab")), default=None) for s in o]
    tabs = [t for t in tabs if t]
    mem = r["rssSeries"]
    return [title, f"{fmt(lat[2] if len(lat) > 2 else None, 0)} → {fmt(lat[-1] if lat else None, 0)}",
            f"{fmt(max(backlog) if backlog else None)} MB", fmt(replaced[-1] if replaced else None, 0),
            f"{fmt(series_at(mem, 2), 0)} → {fmt(mem[-1][1] if mem else None, 0)}",
            f"{fmt(tabs[2] if len(tabs) > 2 else None, 0)} → {fmt(tabs[-1] if tabs else None, 0)}" if tabs else "–",
            fmt(stage(r, "client_mbps"), 1)]


def main():
    out = ["# Step 11 analysis\n",
           "Firefox 159 (snap) on the same machine, headed, driven by geckodriver; the browser viewer is `build/wasm` "
           "(Qt 6.12 `wasm_multithread`, Emscripten 5.0.5, Release). Benchmarks run 20 s after 3 s unless stated. "
           "*Heap* in the browser is the WebAssembly memory (grows, never shrinks); on the desktop it is RSS.\n"]

    # A. Transport
    rows = []
    for title, prefix in (("TCP", "native-tcp-final"), ("WebSocket", "native-ws-final")):
        rs = runs(prefix)
        if rs:
            rows.append([title, str(len(rs)), rng([r["latencyMs"]["p50"] for r in rs]), rng([r["latencyMs"]["p99"] for r in rs]),
                         rng([shown(r) for r in rs]), rng([stage(r, "client_mbps") for r in rs])])
    out.append("## A. The WebSocket endpoint (desktop client, loopback, nominal load)\n\n" + table(
        ["Transport", "n", "Latency p50 ms", "Latency p99 ms", "Lidar frames/s shown", "Received MB/s"], rows) + "\n")

    # B. Clocks
    rows = []
    for title, prefix in (("Browser client, as built", "browser-client-asbuilt"), ("Browser client, clock probes", "browser-client-final4m"),
                          ("Desktop client, server clock +5 s, as built", "native-skew-asbuilt"),
                          ("Desktop client, server clock +5 s, clock probes", "native-skew"),
                          ("Desktop client, server clock −5 s, as built", "native-behind-asbuilt"),
                          ("Desktop client, server clock −5 s, clock probes", "native-behind")):
        rs = runs(prefix)
        if rs:
            rows.append([title, str(len(rs)), rng([r["latencyMs"]["p50"] for r in rs]),
                         rng([stage(r, "client_clock_offset_ms") for r in rs], 2), rng([stage(r, "client_clock_rtt_ms") for r in rs], 2),
                         rng([shown(r) for r in rs])])
    out.append("## B. Clocks\n\n" + table(["Run", "n", "Latency p50 ms (as the client measures it)", "Offset found ms",
                                          "Round trip of the probe in use ms", "Lidar frames/s shown"], rows) + "\n")
    rows = []
    for skew in (0, 5000, -5000):
        for title, name in ((f"as built", f"native-delay{skew}-asbuilt-r1"), ("clock probes", f"native-delay{skew}-r1")):
            r = load(name)
            if r:
                rows.append([f"{skew:+d} ms" if skew else "0", title, fmt(shown(r)), fmt(r["latencyMs"]["p50"])])
    out.append("With a 200 ms presentation delay (desktop client, 10 s after 3 s). As built, frames stamped 5 s in the "
               "future wait 5 s to be shown, and the latency the client measures (224 ms) does not show it:\n\n" + table(
                   ["Server clock", "Client", "Lidar frames/s shown", "Latency p50 ms (measured)"], rows) + "\n")

    # C. Main thread
    rows = [main_thread_row(t, runs(p)) for t, p in (
        ("Desktop client (WebSocket)", "native-ws-final"),
        ("Browser, idle page (no data)", "browser-idle-final"),
        ("Browser client, no labels (201 objects)", "browser-client-nolabels-final"),
        ("Browser client", "browser-client-final4m"),
        ("Browser, own simulator", "browser-sim-final"))]
    out.append("## C. The main thread\n\nNominal load, cameras off. In the browser the GUI-lag probe ticks every 4 ms "
               "(browsers clamp repeating timers to 4 ms; a 2 ms probe read 2 ms of lag on an idle page).\n\n" + table(
                   ["Run", "n", "GUI lag p99 ms", "Frame p99 ms", "Sync p99 ms", "Render mean ms", "Labels ms",
                    "Track model ms", "Latency p50 ms", "Lidar frames/s shown", "Heap MB"], [r for r in rows if r]) + "\n")
    rows = [main_thread_row(t, runs(p)) for t, p in (
        ("Browser, a layout per label", "browser-client"), ("Browser, one layout for all labels", "browser-client-onelayout"),
        ("Desktop, a layout per label", "native-ws"), ("Desktop, one layout for all labels", "native-ws-onelayout"))]
    out.append("Labels, before and after laying them all out at once (the desktop rows: `native-ws-r1..3` timed the "
               "whole overlay sync, which is labels plus 0.03 ms):\n\n" + table(
                   ["Run", "n", "GUI lag p99 ms", "Frame p99 ms", "Sync p99 ms", "Render mean ms", "Labels ms",
                    "Track model ms", "Latency p50 ms", "Lidar frames/s shown", "Heap MB"], [r for r in rows if r]) + "\n")
    rs = runs("native-ws")
    if rs:
        out.append("Desktop overlay sync with a layout per label: " + rng([stage(r, "overlay_sync_ms") for r in rs], 2) + " ms.\n")

    # D. Falling behind
    rows = [behind_row(t, n) for t, n in (
        ("Desktop TCP, as built", "native-slow-tcp-r1"), ("Desktop TCP, credit", "native-slow-tcp-final4m-r1"),
        ("Desktop WebSocket, as built", "native-slow-ws-r1"), ("Desktop WebSocket, credit", "native-slow-ws-final4m-r1"),
        ("Browser, as built", "browser-slow-browser-r1"), ("Browser, credit (run 1)", "browser-slow-browser-final4m-r1"),
        ("Browser, credit (run 2)", "browser-slow-browser-final4m-r2"), ("Browser, credit (run 3)", "browser-slow-browser-final4m-r3"))]
    out.append("## D. A client that falls behind\n\nThe client spends 50 ms on every lidar record (`--set clientDelayMs=50`: "
               "about half of the 40 a second), 75 s after 3 s; the browser's history pinned to 64 MB. Outside: the serving "
               "viewer's backlog for the client and the frames it replaced (its metrics endpoint), and Firefox's tab process (`/proc`). "
               "Credit window: 4 MB (the default).\n\n"
               + table(["Client", "Latency ms, 3 s → end", "Server backlog (max)", "Server replaced (total)", "Heap MB, start → end",
                         "Firefox tab RSS MB", "Received MB/s"], [r for r in rows if r]) + "\n")
    rows = [behind_row(t, n) for t, n in (
        ("Tab hidden 20–50 s", "browser-hidden-final-r1"), ("Main thread 60% busy 20–50 s", "browser-busy-final-r1"),
        ("Peak load (4 × 300k @ 20 Hz, 800 objects, 2,000 events/s), 4 MB window", "browser-peak-w4096-r1"))]
    other = rows
    rows = []
    for w, label in ((1024, "1 MB"), (4096, "4 MB"), (8192, "8 MB"), (1048576, "1 GB (no limit)")):
        rs = runs(f"browser-peak-w{w}")
        sl = runs(f"browser-slow-browser-w{w}")
        if rs or sl:
            rows.append([label, str(len(rs)), rng([shown(r) for r in rs]), rng([r["latencyMs"]["p50"] for r in rs], 0),
                         rng([max(v for _, v in r["latencySeries"]) for r in rs], 0), rng([r["rssSeries"][-1][1] for r in rs], 0),
                         rng([r["latencyMs"]["p50"] for r in sl], 0) if sl else "–"])
    out.append("The credit window (`--set creditWindowKb`), browser client. Peak load: 4 × 300k @ 20 Hz, 800 objects, "
               "2,000 events/s, 75 s; slow: as above, one run each:\n\n" + table(
                   ["Window", "Peak runs", "Peak: lidar frames/s shown (of 80)", "Peak: latency p50 ms", "Peak: latency max ms",
                    "Peak: heap at end MB", "Slow client: latency p50 ms"], rows) + "\n")
    out.append("Other ways for a tab to fall behind, with credit (hidden and busy ran with a 1 MB window; the client kept up "
               "in both, so the window was not what limited them):\n\n" + table(
        ["Browser", "Latency ms, 3 s → end", "Server backlog (max)", "Server replaced (total)", "Heap MB, start → end",
         "Firefox tab RSS MB", "Received MB/s"], [r for r in other if r]) + "\n")
    for name, title in (("browser-hidden-final-r1", "hidden"), ("browser-busy-final-r1", "busy"), ("browser-peak-w4096-r1", "peak")):
        r = load(name)
        if r:
            fps = r["fpsSeries"]
            lat = r["latencySeries"]
            mid = [v for t, v in lat if 22 <= t <= 50]
            out.append(f"- **{title}:** fps in 22–50 s {rng(fps[22:50], 0)}; latency in 22–50 s {rng(mid, 0)} ms; "
                       f"after 52 s {rng([v for t, v in lat if t >= 52], 0)} ms; lidar frames/s shown {fmt(shown(r))}.")
    out.append("")

    # E. Memory
    r = load("browser-client-fill-r1")
    if r:
        heap = r["rssSeries"]
        out.append("## E. History budget in the browser\n\nBudget = 30% of the smaller of what the browser admits to "
                   "(`navigator.deviceMemory`; Firefox: nothing) and the 4 GB WebAssembly address space, capped at 1 GB. "
                   f"90 s run: heap {fmt(heap[0][1], 0)} MB at the start, {fmt(series_at(heap, 12), 0)} MB at 12 s, "
                   f"{fmt(heap[-1][1], 0)} MB at the end; GUI lag p99 {fmt(r['eventLoopLagMs']['p99'], 2)} ms; "
                   f"frame p99 {fmt(r['frameMs']['p99'])} ms; lidar frames/s shown {fmt(shown(r))}. "
                   "As built (no `/proc` in a page) the budget fell back to its minimum, 64 MB.\n")

    # F. Start-up
    s = RAW / "startup.json"
    if s.exists():
        d = json.loads(s.read_text())
        rows = []
        for build in ("pool4", "pool10"):
            for visit in ("first", "reload"):
                rs = [x for x in d["runs"] if x["build"] == build and x["visit"] == visit and not x.get("failed")]
                if rs:
                    rows.append([build.replace("pool", "Pool "), "first visit (fresh profile)" if visit == "first" else "reload",
                                 str(len(rs)), rng([x["loadedMs"] for x in rs], 0), rng([x["mainMs"] for x in rs], 0),
                                 rng([x["qmlMs"] for x in rs], 0), rng([x["firstFrameMs"] for x in rs], 0),
                                 fmt(st.median([x["firstFrameMs"] for x in rs]), 0)])
        files = ", ".join(f"{k} {v / 1e6:.1f} MB" for k, v in sorted(d["files"].items(), key=lambda kv: -kv[1]) if v > 1e5)
        out.append("## F. Start-up (ms from the page's navigation start, localhost)\n\n" + table(
            ["Web Worker pool", "Visit", "n", "Page loaded", "main()", "QML loaded", "First frame", "First frame median"], rows)
            + f"\n\nFiles: {files}. The desktop viewer's first frame: 250 ms after the process starts (step 9).\n")

    out.append("## G. Layout across a reload\n\nSee [`raw/layout-reload.txt`](raw/layout-reload.txt) and "
               "[`layout-reload.png`](layout-reload.png).\n")
    out.append("## H. Old and new viewers\n\nSee [`compat.md`](compat.md).\n")
    (ROOT / "results/step-11/analysis.md").write_text("\n".join(out))
    print("\n".join(out))


if __name__ == "__main__":
    main()
