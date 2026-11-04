"""A process plugin that misbehaves on purpose: python3 misbehave.py <mode> [after]

crash    killed by SIGSEGV after <after> updates
hang     stops answering after <after> updates
flood    sends overlays as fast as it can, 5,000 segments each
garbage  writes random bytes after <after> updates
curious  reports what it can reach: the home directory, a file, the network
"""

import os
import random
import signal
import socket
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "sdk", "python"))
import qpwire  # noqa: E402

mode = sys.argv[1]
after = int(sys.argv[2]) if len(sys.argv) > 2 else 30
count = 0
reported = False


def attempt(what, f):
    try:
        return f"{what}: {f()}"
    except Exception as e:  # noqa: BLE001
        return f"{what}: blocked ({type(e).__name__})"


def curious(port):
    home = os.path.expanduser("~")
    return [attempt("home", lambda: f"{len(os.listdir(home))} entries in {home}"),
            attempt("file", lambda: f"read {len(open('/etc/hostname').read())} bytes of /etc/hostname"),
            attempt("network", lambda: (socket.create_connection(("127.0.0.1", port), timeout=1).close(),
                                        f"connected to 127.0.0.1:{port}")[1])]


def update(tracks, draw, state):
    global count, reported
    count += 1
    if mode == "crash" and count > after:
        os.kill(os.getpid(), signal.SIGSEGV)
    if mode == "hang" and count > after:
        time.sleep(10**6)
    if mode == "garbage" and count > after:
        sys.stdout.buffer.write(random.randbytes(4096))
        sys.stdout.buffer.flush()
    if mode == "curious":
        lines = curious(int(state["info"].get("properties", {}).get("port", 8765)))
        if not reported:
            for line in lines:
                print("curious process:", line, file=sys.stderr, flush=True)
            reported = True
        draw.color("#F87171")
        for i, line in enumerate(lines):
            draw.text(-60, 40 - 4 * i, line)


if mode == "flood":
    # A valid stream, far too much of it: answers come as fast as they can be written, each one
    # 5,000 segments, for the newest tracks seen; stdin is drained on a thread.
    import json
    import struct
    import threading

    inp, out = sys.stdin.buffer, sys.stdout.buffer
    _, _, _, size = struct.unpack("<8sHHI", qpwire._read(inp, 16))
    qpwire._read(inp, size)
    info = json.dumps({"name": "misbehave flood", "api": "1.0"}).encode()
    out.write(struct.pack("<8sHHI", qpwire.MAGIC, 1, 0, len(info)) + info)
    newest = [0]

    def drain():
        while True:
            kind, _, _, _, t, n = struct.unpack("<HHHHqI", qpwire._read(inp, 20))
            qpwire._read(inp, n)
            if kind == qpwire.TRACKS:
                newest[0] = t

    threading.Thread(target=drain, daemon=True).start()
    o = qpwire.Overlay()
    o.color("#F87171")
    for i in range(5000):
        o.line(-25 + i * 0.01, -30, -25 + i * 0.01, -29)
    payload = o.encode()
    try:
        while True:
            out.write(struct.pack("<HHHHqI", qpwire.OVERLAY, 1, 0, 0, newest[0], len(payload)) + payload)
    except (BrokenPipeError, EOFError):
        sys.exit(0)

qpwire.run(update, f"misbehave {mode}")
