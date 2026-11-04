#!/usr/bin/env python3
"""Drives Firefox through geckodriver (WebDriver over HTTP; no selenium needed).

Snap's geckodriver can't be signalled from outside its confinement, so one instance on a fixed port
is started once and reused by every run; each run is its own browser session.

    with Firefox(headless=False, size=(1600, 900)) as ff:
        ff.open(url); ff.screenshot(path); ff.run("return navigator.userAgent")
"""

import base64
import json
import os
import shutil
import subprocess
import time
import urllib.request


class Firefox:
    def __init__(self, headless=False, size=(1600, 900), prefs=None):
        self.headless, self.size, self.prefs = headless, size, prefs or {}
        self.port = int(os.environ.get("GECKODRIVER_PORT", 4444))
        self.session = None

    def _call(self, method, path, body=None, timeout=60):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read() or b"{}").get("value")

    def __enter__(self):
        try:
            self._call("GET", "/status", timeout=1)
        except OSError:
            exe = shutil.which("geckodriver") or shutil.which("firefox.geckodriver")
            subprocess.Popen([exe, "--port", str(self.port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             start_new_session=True)
        for _ in range(100):
            try:
                self._call("GET", "/status", timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        args = ["-headless"] if self.headless else []
        env = {} if self.headless else {"MOZ_ENABLE_WAYLAND": os.environ.get("MOZ_ENABLE_WAYLAND", "1")}
        caps = {"capabilities": {"alwaysMatch": {"browserName": "firefox", "moz:firefoxOptions": {
            "args": args, "prefs": self.prefs, "env": env}}}}
        self.session = self._call("POST", "/session", caps, timeout=120)["sessionId"]
        w, h = self.size
        self._call("POST", f"/session/{self.session}/window/rect", {"width": w, "height": h, "x": 0, "y": 0})
        return self

    def __exit__(self, *exc):
        try:
            if self.session:
                self._call("DELETE", f"/session/{self.session}", timeout=30)
        except OSError:
            pass

    def open(self, url):
        self._call("POST", f"/session/{self.session}/url", {"url": url}, timeout=120)

    def run(self, script, *args):
        return self._call("POST", f"/session/{self.session}/execute/sync", {"script": script, "args": list(args)})

    def screenshot(self, path):
        png = self._call("GET", f"/session/{self.session}/screenshot")
        with open(path, "wb") as f:
            f.write(base64.b64decode(png))

    def new_tab(self):
        h = self._call("POST", f"/session/{self.session}/window/new", {"type": "tab"})["handle"]
        return h

    def switch(self, handle):
        self._call("POST", f"/session/{self.session}/window", {"handle": handle})

    def handles(self):
        return self._call("GET", f"/session/{self.session}/window/handles")

    def key(self, value):
        """Press and release one key; WebDriver key codes, e.g. "\\uE032" for F2."""
        self._call("POST", f"/session/{self.session}/actions", {"actions": [{"type": "key", "id": "kb", "actions": [
            {"type": "keyDown", "value": value}, {"type": "keyUp", "value": value}]}]})

    def click(self, x, y):
        self._call("POST", f"/session/{self.session}/actions", {"actions": [{"type": "pointer", "id": "mouse", "actions": [
            {"type": "pointerMove", "x": x, "y": y}, {"type": "pointerDown", "button": 0}, {"type": "pointerUp", "button": 0}]}]})
