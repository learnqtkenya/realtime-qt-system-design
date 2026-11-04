#!/usr/bin/env python3
"""A second viewer over TCP and over WebSockets, from the same serving viewer (nominal load).

    python3 scripts/step11_wire.py [repeats] [suffix]   -> results/step-11/raw/native-{tcp,ws}[-suffix]-rN.json
"""

import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "build/release/app/viewer"
RAW = ROOT / "results/step-11/raw"
RAW.mkdir(parents=True, exist_ok=True)
repeats = int(sys.argv[1]) if len(sys.argv) > 1 else 3
suffix = "-" + sys.argv[2] if len(sys.argv) > 2 else ""
TARGETS = {"tcp": "127.0.0.1:7420", "ws": "ws://127.0.0.1:7421"}

server = subprocess.Popen([str(APP), "--sensors", "4", "--points", "150000", "--objects", "200", "--events", "200",
                           "--set", "serve=7420", "--set", "serveWs=7421", "--set", "cameras=0", "--set", "layout=0"],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
try:
    time.sleep(3)
    for r in range(1, repeats + 1):
        for name, target in TARGETS.items():
            out = RAW / f"native-{name}{suffix}-r{r}.json"
            p = subprocess.run([str(APP), "--bench", "--warmup", "3", "--duration", "20", "--label", f"native-{name}",
                                "--out", str(out), "--no-sim", "--set", f"connect={target}", "--set", "cameras=0"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
            res = json.loads(out.read_text())
            st = res["stages"]
            print(name, r, "latency p50/p99 %.1f/%.1f" % (res["latencyMs"]["p50"], res["latencyMs"]["p99"]),
                  "lidar/s %.1f" % (st.get("uploaded", {}).get("n", 0) / res["measuredSeconds"]),
                  "MB/s %.1f" % (st.get("client_mbps", {}).get("mean") or 0),
                  "stderr:", [l for l in p.stdout.splitlines() if l.strip()][:3], flush=True)
finally:
    os.killpg(server.pid, signal.SIGTERM)
    sout, _ = server.communicate(timeout=30)
    print("server exit", server.returncode, [l for l in sout.splitlines() if l.strip()][:5])
