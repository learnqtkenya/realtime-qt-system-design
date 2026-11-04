#!/usr/bin/env python3
"""Step 7: a serving viewer plus client viewers in other processes, each writing its own report.

    python3 scripts/twoproc.py [--series ...] [--only label ...] [--summarise-only]
"""

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench  # noqa: E402

VIEWER = bench.BUILD / "app/viewer"
OUT = bench.ROOT / "results/step-07/raw"
NOMINAL = ["--points", "150000", "--rate", "10", "--objects", "200", "--events", "200", "--variant", "rhi",
           "--set", "cameras=0"]
ENV = {**os.environ, "QSG_RHI_PROFILE": "1"}


def setting(**kv):
    return [a for k, v in kv.items() for a in ("--set", f"{k}={v}")]


def run(label, server_args, clients, duration=20, server_stop_at=None, restart_server_at=None):
    """clients: list of (name, extra args, start delay s, stop after s or None)."""
    port = 7400
    procs = []

    def start(name, args, dur):
        out = OUT / f"{label}-{name}.json"
        out.unlink(missing_ok=True)
        cmd = [str(VIEWER), "--bench", "--warmup", "3", "--duration", str(dur), "--label", f"{label}-{name}",
               "--out", str(out), *map(str, args)]
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=ENV)
        procs.append((name, out, p))
        return p

    print(f"{time.strftime('%H:%M:%S')} [{label}]", flush=True)
    t0 = time.time()
    start("server", [*NOMINAL, *setting(serve=port), *server_args], server_stop_at or duration)
    events = sorted([(d, "client", c) for c in clients for d in [c[2]]], key=lambda e: e[0])
    if restart_server_at:
        events.append((restart_server_at, "server2", None))
        events.sort(key=lambda e: e[0])
    for at, kind, c in events:
        time.sleep(max(0, at - (time.time() - t0)))
        if kind == "server2":
            start("server2", [*NOMINAL, *setting(serve=port), *server_args], duration - restart_server_at)
        else:
            name, args, delay, stop = c
            start(name, [*NOMINAL, "--no-sim", *setting(connect=f"127.0.0.1:{port}"), *args],
                  (stop or duration) - delay - 3 if stop else duration - delay - 3)
    for name, out, p in procs:
        try:
            stdout, _ = p.communicate(timeout=duration + 60)
        except subprocess.TimeoutExpired:
            p.kill()
            stdout = "timeout"
        lines = [l for l in stdout.splitlines() if l.strip()]
        if out.exists():
            r = json.loads(out.read_text())
            r["stderrLines"] = lines[:50]
            r["exitStatus"] = p.returncode
            out.write_text(json.dumps(r, indent=2))
        if lines:
            print(f"  [{label}-{name}] {len(lines)} stderr line(s): {lines[0][:150]}", flush=True)


def series():
    s = {}
    s["encode"] = [(f"encode-zstd{z}-r{n}", setting(wireZstd=z), [("client", [], 2, None)]) for n in (1, 2) for z in (0, 1)]
    s["clients"] = [(f"clients{k}-r{n}", [], [(f"client{i}", [], 2, None) for i in range(k)]) for n in (1, 2)
                    for k in (1, 2, 4)]
    s["slow"] = [(f"slow-{pol}-r{n}", setting(sendPolicy=pol),
                  [("slow", setting(readMbps=10), 2, None), ("fast", [], 2, None)]) for n in (1, 2) for pol in ("all", "latest")]
    # Where the slow client's remaining latency queues: our 1 MB high water, or the kernel's buffers?
    small = setting(sendPolicy="latest", highWaterKb=256, sendBufferKb=256)
    s["buffers"] = [(f"buffers-small-r{n}", small, [("slow", setting(readMbps=10, receiveBufferKb=256), 2, None),
                                                     ("fast", [], 2, None)]) for n in (1, 2)]
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--series", nargs="*")
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    all_series = series()
    for name in [n for n in (a.series or list(all_series)) if n in all_series]:
        for label, server_args, clients in all_series[name]:
            if a.only and label not in a.only:
                continue
            run(label, server_args, clients, duration=30 if name == "slow" else 20)
    # Server restart while a client stays: does it come back?
    if not a.series or "restart" in a.series:
        for n in (1, 2):
            # The first server's process lives warmup (3 s) + 6 s, then exits; the second starts at 12 s.
            run(f"restart-r{n}", [], [("client", [], 2, None)], duration=24, server_stop_at=6, restart_server_at=12)


if __name__ == "__main__":
    main()
