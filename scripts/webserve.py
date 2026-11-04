#!/usr/bin/env python3
"""Serves the browser build (build/wasm) on localhost with the headers threads need.

    python3 scripts/webserve.py [--port 8000] [--root build/wasm] [--out results/step-11/raw]

- GET: files under the root, with COOP/COEP (SharedArrayBuffer needs cross-origin isolation).
- PUT /results/<name>.json: a benchmark report from the page, written to --out.
- POST /log/<session>: the page's console lines, appended to <out>/<session>.log.
"""

import argparse
import re
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAME = re.compile(r"^[A-Za-z0-9._-]{1,120}$")


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm", ".mjs": "text/javascript",
                      ".js": "text/javascript", ".json": "application/json"}

    def __init__(self, *a, out: Path, **kw):
        self.out = out
        super().__init__(*a, **kw)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass

    def body(self):
        n = int(self.headers.get("Content-Length", 0))
        if n > 64 << 20:
            raise ValueError("too large")
        return self.rfile.read(n)

    def target(self, prefix):
        name = self.path[len(prefix):]
        if not self.path.startswith(prefix) or not NAME.match(name):
            self.send_error(404)
            return None
        return self.out / name

    def do_PUT(self):
        t = self.target("/results/")
        if t:
            t.write_bytes(self.body())
            self.send_response(204)
            self.end_headers()

    def do_POST(self):
        t = self.target("/log/")
        if t:
            with open(t.with_suffix(".log"), "ab") as f:
                f.write(self.body())
            self.send_response(204)
            self.end_headers()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--root", default=str(ROOT / "build/wasm"))
    ap.add_argument("--out", default=str(ROOT / "results/step-11/raw"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", a.port), partial(Handler, directory=a.root, out=out))
    print(f"serving {a.root} on http://127.0.0.1:{a.port}/app/index.html", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
