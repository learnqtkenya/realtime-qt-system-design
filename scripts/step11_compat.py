#!/usr/bin/env python3
"""Old and new viewers over the wire after step 11 (clock probes, credit, two new record types).

    python3 scripts/step11_compat.py <step-10 viewer>   -> results/step-11/compat.md (+ raw/compat-*.json)

Build the old viewer from the tag first, e.g. git worktree add <dir> step-10 && cmake --preset release -B <dir>/build.
Each pair: the server at nominal load, cameras off; the client benchmarks 10 s after 3 s.
"""

import json
import os
import signal
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NEW = ROOT / "build/release/app/viewer"
RAW = ROOT / "results/step-11/raw"
NOMINAL = ["--sensors", "4", "--points", "150000", "--objects", "200", "--events", "200"]


def pair(name, server_exe, client_exe):
    server = subprocess.Popen([str(server_exe), *NOMINAL, "--set", "serve=7420", "--set", "cameras=0", "--set", "layout=0",
                               "--set", "metricsPort=9100"],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
    time.sleep(3)
    out = RAW / f"compat-{name}.json"
    c = subprocess.Popen([str(client_exe), "--bench", "--warmup", "3", "--duration", "10", "--out", str(out), "--no-sim",
                          "--set", "connect=127.0.0.1:7420", "--set", "cameras=0"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(8)  # mid-run: the server lists connected clients only
    try:
        status = json.loads(urllib.request.urlopen("http://127.0.0.1:9100/bundle", timeout=3).read())["state"]["network"]["status"]
    except Exception as e:  # noqa: BLE001
        status = [f"(no status: {e})"]
    cout, _ = c.communicate(timeout=60)
    os.killpg(server.pid, signal.SIGTERM)
    sout, _ = server.communicate(timeout=30)
    r = json.loads(out.read_text())
    st = r["stages"]
    return {"lidarPerS": st.get("uploaded", {}).get("n", 0) / r["measuredSeconds"], "latencyP50": r["latencyMs"]["p50"],
            "clockProbes": st.get("client_clock_rtt_ms", {}).get("n", 0), "serverStatus": status,
            "stderr": [l for l in (cout + sout).splitlines() if l.strip()][:5], "serverExit": server.returncode}


def main():
    old = Path(sys.argv[1])
    rows = [("old client (step-10), new server", "old-client", NEW, old),
            ("new client, old server (step-10)", "old-server", old, NEW),
            ("new client, new server", "new-new", NEW, NEW)]
    lines = ["# Step 11: old and new viewers over the wire\n",
             "Server at nominal load (4 × 150k @ 10 Hz, 200 objects, 200 events/s), cameras off; the client benchmarks "
             f"10 s after 3 s over TCP (`scripts/step11_compat.py`; old = `step-10` built from the tag).\n",
             "| Pair | Lidar frames/s shown | Latency p50 | Seconds with a probed clock | Server's line for the client | stderr |",
             "|---|---|---|---|---|---|"]
    for title, name, server, client in rows:
        res = pair(name, server, client)
        print(title, res, flush=True)
        lines.append(f"| {title} | {res['lidarPerS']:.1f} | {res['latencyP50']:.1f} ms | {res['clockProbes']} | "
                     f"{'; '.join(res['serverStatus']) or '–'} | {'; '.join(res['stderr']) or 'nothing'} |")
    lines.append("\nThe browser has no TCP: a page needs a server with the WebSocket endpoint (step 11 or later).\n")
    (ROOT / "results/step-11/compat.md").write_text("\n".join(lines))


if __name__ == "__main__":
    main()
