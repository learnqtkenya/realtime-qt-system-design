#!/usr/bin/env python3
"""Step 9 tables from results/step-09/raw -> results/step-09/analysis.md (and soak.png if matplotlib is there)."""

import json
import math
import statistics as st
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "results/step-09/raw"
OUT = ROOT / "results/step-09"

SOAK_KEYS = [("qp_process_rssMb", "RSS (MB)", 0), ("qp_process_heapInUseMb", "Heap in use (MB)", 0),
             ("qp_process_heapFreeMb", "Heap free (MB)", 0), ("qp_process_threads", "Threads", 0),
             ("qp_process_openFiles", "Open files", 0), ("qp_process_cpuPercent", "CPU (%)", 0),
             ("qp_frames_fps", "fps", 1), ("qp_frames_frameMsP99", "Frame p99 (ms)", 1),
             ("qp_frames_lagMsP99", "GUI lag p99 (ms)", 2), ("qp_frames_latencyMsMean", "Latency mean (ms)", 1),
             ("qp_timeline_historyMb", "History (MB)", 0), ("qp_timeline_eventsKept", "Events kept", 0),
             ("qp_diagnostics_sampleMs", "Diagnostics sample (ms)", 2), ("qp_stages_storedValues", "Stage values kept", 0)]


def fmt(v, d=1):
    return "–" if v is None else f"{v:,.{d}f}"


def table(header, rows):
    return "\n".join(["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
                     + ["| " + " | ".join(r) + " |" for r in rows])


def slope(points):
    if len(points) < 3:
        return None
    xs, ys = zip(*points)
    mx, my = st.mean(xs), st.mean(ys)
    den = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else None


SERIES = (("server", "Serving viewer", "#2a78d6"), ("client", "Client", "#eb6834"))
PANELS = (("qp_process_rssMb", "RSS, MB", 0), ("qp_frames_lagMsP99", "GUI lag p99, ms (rolling)", 1),
          ("qp_process_threads", "Threads", 0))


def soak_svg(rows):
    """Small multiples, one measure per panel, each with its own axis; light surface."""
    w, ph, left, right, top, gap = 800, 150, 64, 160, 34, 46
    h = top + len(PANELS) * (ph + gap)
    tmax = max(r["t"] for r in rows) / 60
    ink, muted, grid = "#2b2b29", "#6b6a63", "#e6e5e0"
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
         f'font-family="system-ui, sans-serif" font-size="12">',
         f'<rect width="{w}" height="{h}" fill="#fcfcfb"/>']
    x0 = left
    lx = x0
    for _, name, colour in SERIES:  # legend
        o.append(f'<rect x="{lx}" y="10" width="14" height="3" rx="1.5" fill="{colour}"/>'
                 f'<text x="{lx + 20}" y="15" fill="{ink}">{name}</text>')
        lx += 150
    for i, (key, title, d) in enumerate(PANELS):
        y0 = top + i * (ph + gap) + 18
        vals = [r[who][key] for r in rows for who, _, _ in SERIES if r.get(who) and key in r[who]]
        if not vals:
            continue
        raw = max(vals) / 4 or 1
        mag = 10 ** math.floor(math.log10(raw))
        step = next(m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw)
        lo, hi = 0.0, step * 4
        sx = lambda t: x0 + (w - left - right) * (t / tmax if tmax else 0)  # noqa: E731
        sy = lambda v: y0 + ph - ph * (v - lo) / (hi - lo)  # noqa: E731
        o.append(f'<text x="{x0}" y="{y0 - 6}" fill="{ink}" font-weight="600">{title}</text>')
        for k in range(5):
            v = lo + (hi - lo) * k / 4
            o.append(f'<line x1="{x0}" x2="{w - right}" y1="{sy(v):.1f}" y2="{sy(v):.1f}" stroke="{grid}" stroke-width="1"/>'
                     f'<text x="{x0 - 6}" y="{sy(v) + 4:.1f}" fill="{muted}" text-anchor="end">{v:,.{d}f}</text>')
        for m in range(0, int(tmax) + 1, 10):
            o.append(f'<text x="{sx(m):.1f}" y="{y0 + ph + 16}" fill="{muted}" text-anchor="middle">{m} min</text>')
        for who, name, colour in SERIES:
            pts = [(r["t"] / 60, r[who][key]) for r in rows if r.get(who) and key in r[who]]
            if not pts:
                continue
            path = " ".join(f"{sx(t):.1f},{sy(v):.1f}" for t, v in pts)
            o.append(f'<polyline points="{path}" fill="none" stroke="{colour}" stroke-width="2" '
                     f'stroke-linejoin="round" stroke-linecap="round"><title>{name}: {title}</title></polyline>')
            t, v = pts[-1]
            o.append(f'<text x="{sx(t) + 6:.1f}" y="{sy(v) + 4:.1f}" fill="{ink}">{name} {v:,.{d}f}</text>')
    o.append("</svg>")
    return "\n".join(o)


def soak(prefix, title):
    path = RAW / f"{prefix}-series.jsonl"
    if not path.exists():
        return ""
    rows = [json.loads(l) for l in path.read_text().splitlines()]
    out = [f"## {title}\n\n{len(rows)} samples, one every 10 s, {rows[-1]['t'] / 60:.0f} minutes. "
           "Serving viewer: nominal lidar, four cameras, three layers, one client connected. "
           "Client: a second viewer showing the first one's streams. Both watched only through "
           "their metrics endpoints. *Settled* is minutes 5–10; *end* is the last 5 minutes; *slope* is a "
           "least-squares fit after minute 5, per hour.\n"]
    for who in ("server", "client"):
        series = [(r["t"], r[who]) for r in rows if r.get(who)]
        missing = sum(1 for r in rows if not r.get(who))
        trows = []
        for key, title, d in SOAK_KEYS:
            pts = [(t, m[key]) for t, m in series if key in m]
            if not pts:
                continue
            after = [(t, v) for t, v in pts if t >= 300]
            settled = [v for t, v in pts if 300 <= t < 600]
            end_t = pts[-1][0]
            end = [v for t, v in pts if t >= end_t - 300]
            s = slope(after)
            trows.append([title, fmt(st.mean(settled) if settled else None, d), fmt(st.mean(end) if end else None, d),
                          fmt(s * 3600 if s is not None else None, d + 1), fmt(min(v for _, v in after), d) if after else "–",
                          fmt(max(v for _, v in after), d) if after else "–"])
        bad = Counter()
        for _, m in series:
            if m.get("qp_health_notOk", 0) > 0:
                bad["samples with a stream not ok"] += 1
            if m.get("qp_layers_failing", 0) > 0:
                bad["samples with a layer failing"] += 1
            if who == "server" and m.get("qp_network_peers", 1) < 1:
                bad["samples with no client"] += 1
            if who == "client" and m.get("qp_network_peers", 1) < 1:
                bad["samples disconnected"] += 1
        out.append(f"### {who.capitalize()}\n\n" + table(["Metric", "Settled", "End", "Slope per hour", "Min", "Max"], trows)
                   + f"\n\nUnanswered scrapes: {missing}. " + (", ".join(f"{k}: {v}" for k, v in bad.items()) or
                                                              "Every sample: all streams ok, no layer failing, connected.")
                   + "\n")
    exit_info = RAW / f"{prefix}-exit.json"
    if exit_info.exists():
        out.append("Exit: `" + json.dumps(json.loads(exit_info.read_text())) + "`\n")
    for name in (f"{prefix}-logs", f"{prefix}-logs-client"):
        d = RAW / name
        if d.exists():
            files = sorted(d.glob("viewer.log*"))
            lines = [json.loads(l) for f in files for l in f.read_text().splitlines() if l.strip()]
            cats = Counter((l.get("level"), l.get("category")) for l in lines)
            out.append(f"Log `{name}`: {len(files)} file(s), {sum(f.stat().st_size for f in files) / 1e3:.0f} kB, "
                       f"{len(lines)} lines: " + ", ".join(f"{lv}/{c or '-'} {n}" for (lv, c), n in cats.most_common()) + "\n")
    (OUT / f"{prefix}.svg").write_text(soak_svg(rows))
    out.append(f"![RSS, GUI lag p99 and threads over the soak, serving viewer and client]({prefix}.svg)\n")
    return "\n".join(out)


def startup():
    path = RAW / "startup.json"
    if not path.exists():
        return ""
    rows = json.loads(path.read_text())
    out = []
    for where in dict.fromkeys(r["where"] for r in rows):
        rs = [r for r in rows if r["where"] == where]
        line = [where, str(len(rs))]
        for k in ("mainMs", "qmlLoadedMs", "firstFrameMs"):
            v = [r[k] for r in rs if k in r]
            line.append(f"{st.median(v):.0f} ({min(v):.0f}–{max(v):.0f})" if v else "–")
        line.append(str(sum(1 for r in rs if r["stderr"])))
        out.append(line)
    return ("## Start-up\n\nMilliseconds since the process started (from /proc, 10 ms resolution): median "
            "(min–max). Warm file cache. Nominal load, cameras on.\n\n"
            + table(["Where", "Runs", "main() entered", "QML loaded", "First frame", "Runs with stderr"], out) + "\n")


def metrics_cost():
    out = []
    for hz in (0, 1, 10):
        rs = [json.loads(p.read_text()) for p in sorted(RAW.glob(f"metrics-{hz}hz-r*.json"))]
        if not rs:
            continue
        g = lambda f: st.median([f(r) for r in rs])  # noqa: E731
        out.append([f"{hz} Hz" if hz else "not scraped", str(len(rs)), fmt(g(lambda r: r["eventLoopLagMs"]["p99"]), 2),
                    fmt(g(lambda r: r["frameMs"]["p99"])), fmt(g(lambda r: r["guiCpuPercent"]["mean"])),
                    fmt(g(lambda r: r["processCpuPercent"]["mean"]))])
    if not out:
        return ""
    return ("## Cost of being watched\n\nNominal load, cameras on, 20 s, metrics served on localhost and scraped at "
            "the given rate.\n\n" + table(["Scraped", "n", "GUI lag p99", "Frame p99", "GUI CPU", "Process CPU"], out) + "\n")


def growth():
    path = RAW / "stage-growth.json"
    if not path.exists():
        return ""
    g = json.loads(path.read_text())
    rows = [["every value" if k == "0" else f"newest {int(k):,}", fmt(v["storedValues_per_s"], 0), f"{v['storedValues_end']:,.0f}"]
            for k, v in g.items()]
    return ("## Stage statistics, 4 minutes\n\nStored values, from the metrics endpoint between minute 1 and minute 4 "
            "(three layers, nominal load).\n\n" + table(["Kept", "Growth (values/s)", "Stored at 4 min"], rows) + "\n")


def main():
    parts = ["# Step 9 analysis\n", startup(), metrics_cost(),
             soak("soak1", "Soak 1: stage statistics keep every value (as built)"),
             soak("soak2", "Soak 2: stage statistics keep the newest 10,000 values per stage"), growth()]
    (OUT / "analysis.md").write_text("\n".join(p for p in parts if p))
    print("wrote results/step-09/analysis.md")


if __name__ == "__main__":
    main()
