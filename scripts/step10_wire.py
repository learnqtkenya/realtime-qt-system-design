#!/usr/bin/env python3
"""A second viewer over the wire, with 4 and 8 lidars on the serving viewer.

    python3 scripts/step10_wire.py [suffix]   -> results/step-10/raw/wire-lidars<suffix>.json
"""

import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "build/release/app/viewer"
RAW = ROOT / "results/step-10/raw"
suffix = sys.argv[1] if len(sys.argv) > 1 else ""
res = {}
for k in (4, 8):
    server = subprocess.Popen([str(APP), "--sensors", str(k), "--points", "150000", "--objects", "200", "--events", "200",
                               "--set", "serve=7420", "--set", "cameras=0", "--set", "layout=0", "--set", "metricsPort=9100"],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(3)
    out = RAW / f"wire-{k}lidars{suffix}.json"
    p = subprocess.run([str(APP), "--bench", "--warmup", "3", "--duration", "20", "--label", f"wire-{k}", "--out", str(out),
                        "--no-sim", "--set", "connect=127.0.0.1:7420", "--set", "cameras=0"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
    status = json.loads(urllib.request.urlopen("http://127.0.0.1:9100/bundle", timeout=3).read())
    server.terminate()
    sout, _ = server.communicate(timeout=30)
    r = json.loads(out.read_text())
    st = r["stages"]
    res[k] = {"clientLatencyP50": r["latencyMs"]["p50"], "clientLatencyP99": r["latencyMs"]["p99"],
              "clientLidarPerS": st.get("uploaded", {}).get("n", 0) / r["measuredSeconds"],
              "clientMbps": st.get("client_mbps", {}).get("mean"), "serverStatus": status["state"]["network"]["status"],
              "stderr": [l for l in (p.stdout + sout).splitlines() if l.strip()][:5]}
    print(k, res[k], flush=True)
(RAW / f"wire-lidars{suffix}.json").write_text(json.dumps(res, indent=1))
