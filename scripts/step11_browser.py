#!/usr/bin/env python3
"""The browser viewer (build/wasm) in Firefox, benchmarked like the desktop one, and the clock series.

    python3 scripts/step11_browser.py <series> [repeats] [suffix]   -> results/step-11/raw/<name><suffix>-rN.json (+ .log)

Series (the serving viewer runs nominal load, cameras off; 20 s after 3 s):
    client          the page connected over WebSocket to a desktop viewer
    client-skew     the same, with the serving viewer's clock 5 s ahead (--set clockOffsetMs=5000)
    client-nolabels the page connected to a viewer serving 201 objects: one over the label budget, so no labels
    sim             the page running its own simulator at nominal load (no server)
    idle            the page with no data at all (the browser's own floor)
    client-fill     the client for 90 s, long enough to fill its history budget
    native-skew     a desktop client of a serving viewer whose clock is 5 s ahead
    native-behind   the same, 5 s behind
    native-delay    a desktop client with a 200 ms presentation delay, server clock 0, +5 s and -5 s (10 s each)
"""

import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from browser import Firefox  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "build/release/app/viewer"
RAW = ROOT / "results/step-11/raw"
NOMINAL = ["--sensors", "4", "--points", "150000", "--objects", "200", "--events", "200"]
WS = ["--no-sim", "--set", "connect=ws://127.0.0.1:7421", "--set", "cameras=0"]
SKEW = {"client-skew": 5000, "native-skew": 5000, "native-behind": -5000}


def bench(duration=20):
    return ["--bench", "--warmup", "3", "--duration", str(duration)]


def serve(skew_ms=0, objects=200):
    extra = ["--set", f"clockOffsetMs={skew_ms}"] if skew_ms else []
    load = [("201" if a == "200" and NOMINAL[i - 1] == "--objects" and objects == 201 else a) for i, a in enumerate(NOMINAL)]
    return subprocess.Popen([str(APP), *load, "--set", "serve=7420", "--set", "serveWs=7421", "--set", "cameras=0",
                             "--set", "layout=0", *extra],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)


def stop(p):
    os.killpg(p.pid, signal.SIGTERM)
    out, _ = p.communicate(timeout=30)
    return p.returncode, [l for l in out.splitlines() if l.strip()][:5]


def page(name, args, duration=20):
    out, log = RAW / f"{name}.json", RAW / f"{name}.log"
    for f in (out, log):
        f.unlink(missing_ok=True)
    args = [*bench(duration), "--label", name, "--out", f"http://127.0.0.1:8000/results/{name}.json", *args]
    q = "&".join("a=" + a.replace("&", "%26") for a in args)
    with Firefox(headless=False) as ff:
        ff.open(f"http://127.0.0.1:8000/app/index.html?session={name}&{q}")
        t = time.time()
        while not out.exists() and time.time() - t < duration + 100:
            time.sleep(0.5)
        time.sleep(1)  # the last log batch
    return json.loads(out.read_text()) if out.exists() else None


def native(name, args, duration=20):
    out = RAW / f"{name}.json"
    p = subprocess.run([str(APP), *bench(duration), "--label", name, "--out", str(out), *args],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
    (RAW / f"{name}.log").write_text(p.stdout)
    return json.loads(out.read_text())


def summary(name, r):
    if r is None:
        print(name, "NO REPORT", flush=True)
        return
    st = r["stages"]
    lines = (RAW / f"{name}.log").read_text().splitlines() if (RAW / f"{name}.log").exists() else []
    errs = [l for l in lines if (" err " in l or not name.startswith("browser")) and l.strip()]
    clock = st.get("client_clock_offset_ms", {})
    print(name, "frame p99 %.1f" % r["frameMs"]["p99"], "lag p99 %.2f" % r["eventLoopLagMs"]["p99"],
          "latency p50 %.1f" % r["latencyMs"]["p50"], "lidar/s %.1f" % (st.get("uploaded", {}).get("n", 0) / r["measuredSeconds"]),
          "heap MB %.0f" % r["rssMb"]["max"], "clock offset ms %s" % (round(clock["mean"], 2) if clock.get("n") else "–"),
          "stderr", len(errs), errs[:2], flush=True)


def main():
    series = sys.argv[1]
    repeats = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    suffix = "-" + sys.argv[3] if len(sys.argv) > 3 else ""
    RAW.mkdir(parents=True, exist_ok=True)
    web = subprocess.Popen([sys.executable, str(ROOT / "scripts/webserve.py")], stdout=subprocess.DEVNULL,
                           start_new_session=True)
    try:
        if series == "native-delay":
            for skew in (0, 5000, -5000):
                server = serve(skew)
                time.sleep(3)
                for r in range(1, repeats + 1):
                    name = f"native-delay{skew}{suffix}-r{r}"
                    summary(name, native(name, ["--no-sim", "--set", "connect=127.0.0.1:7420", "--set", "cameras=0",
                                                "--set", "delayMs=200"], duration=10))
                print("server exit", *stop(server), flush=True)
            return
        server = serve(SKEW.get(series, 0), 201 if series == "client-nolabels" else 200) if series not in ("sim", "idle") else None
        time.sleep(3)
        for r in range(1, repeats + 1):
            prefix = series if series.startswith("native") else f"browser-{series}"
            name = f"{prefix}{suffix}-r{r}"
            if series == "sim":
                summary(name, page(name, [*NOMINAL, "--set", "cameras=0"]))
            elif series == "idle":
                summary(name, page(name, ["--no-sim", "--set", "cameras=0"]))
            elif series.startswith("native"):
                summary(name, native(name, ["--no-sim", "--set", "connect=127.0.0.1:7420", "--set", "cameras=0"]))
            elif series == "client-fill":
                summary(name, page(name, WS, duration=90))
            else:
                summary(name, page(name, WS))
        if server:
            print("server exit", *stop(server), flush=True)
    finally:
        os.killpg(web.pid, signal.SIGTERM)


if __name__ == "__main__":
    main()
