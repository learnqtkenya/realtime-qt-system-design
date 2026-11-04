#!/usr/bin/env python3
"""A browser tab that falls behind: hidden, or with a busy main thread.

    [CREDIT_WINDOW_KB=<kb>] python3 scripts/step11_behind.py <hidden|busy|peak|slow-browser|slow-tcp|slow-ws> [repeats] [suffix]
        -> results/step-11/raw/browser-<mode>[-suffix]-rN.json (the page's report) and ...-rN-outside.json

The page runs a 75 s benchmark connected to a desktop viewer serving nominal load. From 20 s to 50 s
the tab is either in the background (another tab in front) or its main thread is 60% busy with a
script. Or (peak) nothing happens to the tab and the server sends peak load (4 × 300k @ 20 Hz,
800 objects, 2,000 events/s) for the whole run. Or (slow-*) the client spends 50 ms on every lidar
record (--set clientDelayMs=50: about half of what arrives) for the whole run: in the page, or in a
desktop client over TCP or WebSocket. The page's history is pinned to 64 MB (--set historyMb=64,
its budget before the browser's limits were used), so memory that grows is the queue's. Outside the page, once a second: the serving viewer's backlog for this client (its metrics
endpoint) and the resident memory of Firefox's content processes (/proc).
"""

import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from browser import Firefox  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "build/release/app/viewer"
RAW = ROOT / "results/step-11/raw"
NOMINAL = ["--sensors", "4", "--points", "150000", "--objects", "200", "--events", "200"]
PEAK = ["--sensors", "4", "--points", "300000", "--rate", "20", "--objects", "800", "--events", "2000"]
START, END, DURATION = 20, 50, 75


def firefox_rss_mb():
    """Resident MB per Firefox process: content processes rename themselves ("…/browser 7 tab")."""
    out = {}
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            cmd = open(f"/proc/{pid}/cmdline", "rb").read().split(b"\0")
            if not cmd or b"/firefox" not in cmd[0] or b"geckodriver" in cmd[0] or b"crashhelper" in cmd[0]:
                continue
            m = re.search(rb"VmRSS:\s+(\d+)", open(f"/proc/{pid}/status", "rb").read())
            kind = cmd[0].rsplit(b" ", 1)[-1].decode() if b"/browser " in cmd[0] else ("parent" if b"-contentproc" not in cmd else "content")
            out[f"{pid} {kind}"] = int(m.group(1)) / 1024 if m else 0
        except OSError:
            continue
    return out


def watch(stop, t0, out):
    while not stop.is_set():
        row = {"t": time.time() - t0}
        try:
            b = json.loads(urllib.request.urlopen("http://127.0.0.1:9100/bundle", timeout=2).read())
            lines = b["state"]["network"]["status"]
            row["server"] = lines
            m = [re.search(r"backlog ([\d.]+) MB · (\d+) replaced", l) for l in lines]
            row["backlogMb"] = max((float(x.group(1)) for x in m if x), default=None)
            row["replaced"] = max((int(x.group(2)) for x in m if x), default=None)
        except Exception as e:  # noqa: BLE001
            row["error"] = str(e)
        row["firefoxRssMb"] = firefox_rss_mb()
        out.append(row)
        stop.wait(1.0)


def run_native(mode, name):
    out = RAW / f"{name}.json"
    out.unlink(missing_ok=True)
    target = "127.0.0.1:7420" if mode == "slow-tcp" else "ws://127.0.0.1:7421"
    outside, stop = [], threading.Event()
    t0 = time.time()
    th = threading.Thread(target=watch, args=(stop, t0, outside), daemon=True)
    th.start()
    p = subprocess.run([str(APP), "--bench", "--warmup", "3", "--duration", str(DURATION), "--label", name, "--out", str(out),
                        "--no-sim", "--set", f"connect={target}", "--set", "cameras=0", "--set", "clientDelayMs=50"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=DURATION + 60)
    stop.set()
    th.join()
    (RAW / f"{name}.log").write_text(p.stdout)
    (RAW / f"{name}-outside.json").write_text(json.dumps({"marks": {}, "series": outside}, indent=1))
    return json.loads(out.read_text())


def run(mode, name):
    if mode in ("slow-tcp", "slow-ws"):
        return run_native(mode, name)
    out = RAW / f"{name}.json"
    out.unlink(missing_ok=True)
    args = ["--bench", "--warmup", "3", "--duration", str(DURATION), "--label", name, "--out",
            f"http://127.0.0.1:8000/results/{name}.json", "--no-sim", "--set", "connect=ws://127.0.0.1:7421",
            "--set", "cameras=0", "--set", "historyMb=64", *(["--set", "clientDelayMs=50"] if mode == "slow-browser" else [])]
    q = "&".join("a=" + a for a in args)
    outside, stop, marks = [], threading.Event(), {}
    with Firefox(headless=False) as ff:
        t0 = time.time()
        th = threading.Thread(target=watch, args=(stop, t0, outside), daemon=True)
        th.start()
        ff.open(f"http://127.0.0.1:8000/app/index.html?session={name}&{q}")
        page = ff.handles()[0]
        time.sleep(3 + START)  # warm-up, then 20 s measured
        marks["start"] = time.time() - t0
        if mode in ("peak", "slow-browser"):
            time.sleep(END - START)
        elif mode == "hidden":
            tab = ff.new_tab()
            ff.switch(tab)
            ff.open("about:blank")
            time.sleep(END - START)
            ff.switch(page)
            marks["visibleAgain"] = ff.run("return !document.hidden")
        else:
            ff.run("""window.__busy = setInterval(() => { const t = performance.now(); while (performance.now() - t < 60) {} }, 100);""")
            time.sleep(END - START)
            ff.run("clearInterval(window.__busy)")
        marks["end"] = time.time() - t0
        while not out.exists() and time.time() - t0 < DURATION + 60:
            time.sleep(0.5)
        stop.set()
        th.join()
    (RAW / f"{name}-outside.json").write_text(json.dumps({"marks": marks, "series": outside}, indent=1))
    return json.loads(out.read_text()) if out.exists() else None


def main():
    mode = sys.argv[1]
    repeats = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    suffix = "-" + sys.argv[3] if len(sys.argv) > 3 else ""
    web = subprocess.Popen([sys.executable, str(ROOT / "scripts/webserve.py")], stdout=subprocess.DEVNULL, start_new_session=True)
    server = subprocess.Popen([str(APP), *(PEAK if mode == "peak" else NOMINAL), "--set", "serve=7420", "--set", "serveWs=7421", "--set", "cameras=0",
                               "--set", "layout=0", "--set", "metricsPort=9100",
                               *(["--set", f"creditWindowKb={os.environ['CREDIT_WINDOW_KB']}"] if os.environ.get("CREDIT_WINDOW_KB") else [])],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
    try:
        time.sleep(3)
        for r in range(1, repeats + 1):
            name = (f"native-{mode}" if mode in ("slow-tcp", "slow-ws") else f"browser-{mode}") + f"{suffix}-r{r}"
            rep = run(mode, name)
            o = json.loads((RAW / f"{name}-outside.json").read_text())
            peak = max((s.get("backlogMb") or 0) for s in o["series"])
            print(name, "report" if rep else "NO REPORT", "marks", o["marks"], "server backlog peak MB", peak, flush=True)
    finally:
        os.killpg(server.pid, signal.SIGTERM)
        sout, _ = server.communicate(timeout=30)
        print("server exit", server.returncode, [l for l in sout.splitlines() if l.strip()][:5])
        os.killpg(web.pid, signal.SIGTERM)


if __name__ == "__main__":
    main()
