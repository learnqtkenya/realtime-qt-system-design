#!/usr/bin/env python3
"""Start-up of the browser viewer: first visit (fresh profile) and reload, Web Worker pool 4 vs 10.

    python3 scripts/step11_startup.py [repeats]   -> results/step-11/raw/startup.json

Builds: build/wasm (pool 4, Qt's default) and build/wasm-pool10 (cmake --preset wasm -B build/wasm-pool10
-DQP_WASM_POOL=10). The page connects to a desktop viewer serving nominal load. Times are from the
page's navigation start: the page's "loaded" (Qt's onLoaded), and the viewer's main(), QML loaded and
first frame.
"""

import json
import os
import re
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
LOGS = RAW / "startup-logs"
ARGS = ["--no-sim", "--set", "connect=ws://127.0.0.1:7421", "--set", "cameras=0", "--set", "layout=0"]
BUILDS = {"pool4": ROOT / "build/wasm", "pool10": ROOT / "build/wasm-pool10"}


def wait_line(log, timeout=60):
    t = time.time()
    while time.time() - t < timeout:
        if log.exists():
            text = log.read_text()
            m = re.search(r"startup: main (\d+) ms, qml (\d+) ms, first frame (\d+) ms", text)
            loaded = re.search(r"page: loaded in (\d+) ms", text)
            if m and loaded:
                return {"loadedMs": int(loaded.group(1)), "mainMs": int(m.group(1)), "qmlMs": int(m.group(2)),
                        "firstFrameMs": int(m.group(3)), "poolExhausted": text.count("thread pool is exhausted")}
        time.sleep(0.2)
    return None


def main():
    repeats = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    LOGS.mkdir(parents=True, exist_ok=True)
    server = subprocess.Popen([str(APP), "--sensors", "4", "--points", "150000", "--objects", "200", "--events", "200",
                               "--set", "serve=7420", "--set", "serveWs=7421", "--set", "cameras=0", "--set", "layout=0"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    results = []
    try:
        time.sleep(3)
        q = "&".join("a=" + a for a in ARGS)
        for r in range(1, repeats + 1):
            for build, root in BUILDS.items():
                web = subprocess.Popen([sys.executable, str(ROOT / "scripts/webserve.py"), "--root", str(root), "--out", str(LOGS)],
                                       stdout=subprocess.DEVNULL, start_new_session=True)
                time.sleep(0.5)
                try:
                    with Firefox(headless=False) as ff:
                        for visit in ("first", "reload"):
                            name = f"{build}-{visit}-r{r}"
                            (LOGS / f"{name}.log").unlink(missing_ok=True)
                            ff.open(f"http://127.0.0.1:8000/app/index.html?session={name}&{q}")
                            row = wait_line(LOGS / f"{name}.log")
                            print(name, row, flush=True)
                            results.append({"build": build, "visit": visit, "run": r, **(row or {"failed": True})})
                            time.sleep(2)
                finally:
                    os.killpg(web.pid, signal.SIGTERM)
    finally:
        os.killpg(server.pid, signal.SIGTERM)
        server.wait(timeout=30)
    sizes = {f.name: f.stat().st_size for f in (ROOT / "build/wasm/app").iterdir() if f.suffix in (".wasm", ".js", ".html", ".json")}
    (RAW / "startup.json").write_text(json.dumps({"runs": results, "files": sizes}, indent=1))


if __name__ == "__main__":
    main()
