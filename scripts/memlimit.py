#!/usr/bin/env python3
"""The viewer under a memory limit (a cgroup via systemd-run), watched through its metrics until it
stops or the time is up. Writes results/step-10/raw/<label>.json.

    python3 scripts/memlimit.py <label> <limit, e.g. 1G> <seconds> [viewer args...]
"""

import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from step09_runs import scrape  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
label, limit, seconds, extra = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4:]
cmd = ["systemd-run", "--user", "--scope", "-q", "-p", f"MemoryMax={limit}", "-p", "MemorySwapMax=0",
       str(ROOT / "build/release/app/viewer"), "--set", "layout=0", "--set", "metricsPort=9100", *extra]
p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
start, series = time.time(), []
while p.poll() is None and time.time() - start < seconds:
    m = scrape(9100)
    if m:
        series.append({"t": round(time.time() - start, 1), "rssMb": m.get("qp_process_rssMb"),
                       "historyMb": m.get("qp_timeline_historyMb"), "historySeconds": m.get("qp_timeline_historySeconds")})
    time.sleep(2)
alive = p.poll() is None
if alive:
    p.terminate()
out, _ = p.communicate(timeout=30)
result = {"label": label, "limit": limit, "args": extra, "survived": alive, "exit": p.returncode,
          "diedAfterS": None if alive else series[-1]["t"] if series else 0, "series": series,
          "output": out.splitlines()[-10:]}
Path(ROOT / f"results/step-10/raw/{label}.json").write_text(json.dumps(result, indent=1))
print(json.dumps({k: v for k, v in result.items() if k != "series"}), series[-1] if series else None)
