#!/usr/bin/env python3
"""Step 9 measurements.

    python3 scripts/step09_runs.py startup [--runs 10]   start-up, build tree vs package (/opt/Qt hidden)
    python3 scripts/step09_runs.py metrics               cost of being scraped (none, 1 Hz, 10 Hz)
    python3 scripts/step09_runs.py soak [--minutes 60] [--name soak2]   a serving viewer and a client, watched from outside

The package must be installed first: cmake --install build/release --prefix build/package
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BUILD_APP = ROOT / "build/release/app/viewer"
PACKAGE_APP = ROOT / "build/package/bin/viewer"
OUT = ROOT / "results/step-09"
RAW = OUT / "raw"


def scrape(port):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/metrics", timeout=2) as r:
            out = {}
            for line in r.read().decode().splitlines():
                name, _, value = line.partition(" ")
                out[name] = float(value)
            return out
    except OSError:
        return None


def startup(runs):
    rows = []
    for n in range(runs):
        for where, cmd in (("build tree", [str(BUILD_APP)]),
                           ("package, /opt/Qt hidden", ["bwrap", "--dev-bind", "/", "/", "--tmpfs", "/opt", "--",
                                                        str(PACKAGE_APP)])):
            bundle = RAW / f"startup-{where.split(',')[0].replace(' ', '-')}-{n}.json"
            proc = subprocess.Popen([*cmd, "--set", "layout=0", "--set", f"bundleOnExit={bundle}", "--points", "150000",
                                     "--objects", "200", "--events", "200"],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd="/tmp",
                                    start_new_session=True)
            time.sleep(4)
            # The whole group: bwrap does not pass signals on to the viewer it started.
            os.killpg(proc.pid, signal.SIGTERM)
            out, _ = proc.communicate(timeout=20)
            b = json.loads(bundle.read_text())
            s = b["state"]["startup"]
            rows.append({"where": where, "run": n, **s, "exit": proc.returncode, "stderr": out.strip()[:200],
                         "rssMb": b["state"]["process"]["rssMb"]})
            print(rows[-1], flush=True)
    (RAW / "startup.json").write_text(json.dumps(rows, indent=1))


def metrics():
    args = ["--points", 150000, "--rate", 10, "--objects", 200, "--events", 200, "--variant", "rhi"]
    for n in (1, 2):
        for hz in (0, 1, 10):
            label = f"metrics-{hz}hz-r{n}"
            stop = threading.Event()
            counts = [0]

            def scraper(stop=stop, counts=counts, hz=hz):  # bound now: the loop rebinds these names
                while not stop.is_set():
                    if scrape(9100):
                        counts[0] += 1
                    stop.wait(1 / hz)

            t = None
            if hz:
                t = threading.Thread(target=scraper, daemon=True)
                threading.Timer(2, t.start).start()
            bench.run(BUILD_APP, label, RAW, [*args, "--set", "metricsPort=9100"], duration=20,
                      env={"QSG_RHI_PROFILE": "1"})
            stop.set()
            print(f"{label}: {counts[0]} scrapes", flush=True)


def soak(minutes, name, stage_cap=None):
    server_bundle = RAW / f"{name}-server-bundle.json"
    client_bundle = RAW / f"{name}-client-bundle.json"
    cap = ["--set", f"stageCap={stage_cap}"] if stage_cap is not None else []
    common = ["--set", "layout=0", *cap, "--set", "logDir=" + str(RAW / f"{name}-logs")]
    server = subprocess.Popen([str(BUILD_APP), "--points", "150000", "--rate", "10", "--objects", "200", "--events", "200",
                               "--variant", "rhi", "--set", "serve=7400", "--set", "metricsPort=9100",
                               "--set", "layers=speed-vectors,range-rings,geofence", "--set", "selectId=1007",
                               "--set", f"bundleOnExit={server_bundle}", *common],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(5)
    client = subprocess.Popen([str(BUILD_APP), "--no-sim", "--set", "connect=127.0.0.1:7400", "--set", "cameras=0",
                               "--set", "metricsPort=9101", "--set", f"bundleOnExit={client_bundle}",
                               "--set", "logDir=" + str(RAW / f"{name}-logs-client"), "--set", "layout=0", *cap],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    series = RAW / f"{name}-series.jsonl"
    start = time.time()
    with series.open("w") as f:
        while time.time() - start < minutes * 60:
            row = {"t": round(time.time() - start, 1), "server": scrape(9100), "client": scrape(9101)}
            f.write(json.dumps(row) + "\n")
            f.flush()
            time.sleep(10)
    for p in (client, server):
        p.send_signal(signal.SIGTERM)
    outs = {}
    for name, p in (("client", client), ("server", server)):
        out, _ = p.communicate(timeout=30)
        outs[name] = {"exit": p.returncode, "stderr": out.splitlines()[:50]}
    (RAW / f"{name}-exit.json").write_text(json.dumps(outs, indent=1))
    print("soak done", outs, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["startup", "metrics", "soak"])
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--minutes", type=float, default=60)
    ap.add_argument("--name", default="soak2")
    ap.add_argument("--stage-cap", help="passed as --set stageCap (soak1: 0, every value kept, as first built)")
    a = ap.parse_args()
    RAW.mkdir(parents=True, exist_ok=True)
    {"startup": lambda: startup(a.runs), "metrics": metrics, "soak": lambda: soak(a.minutes, a.name, a.stage_cap)}[a.what]()


if __name__ == "__main__":
    main()
