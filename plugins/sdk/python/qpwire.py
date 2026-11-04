"""The viewer's process-plugin protocol (API 1.x), in Python.

stdin, from the viewer: a hello, then records. stdout, to the viewer: a hello, then one overlay
record for every tracks record, with the same t, even if it is empty.

    hello:  b"QPSTREAM", u16 major, u16 minor, u32 size, JSON
    record: u16 type, u16 version, u16 stream, u16 flags, i64 t, u32 size, payload

Records of types this code doesn't know are skipped, and fields beyond the stride of an item are
ignored: that is how a 1.0 plugin keeps working with a newer viewer.
"""

import json
import struct
import sys

MAGIC = b"QPSTREAM"
TRACKS, OVERLAY, SELECTION = 2, 6, 7
_HELLO = struct.Struct("<8sHHI")
_RECORD = struct.Struct("<HHHHqI")
_TRACKS = struct.Struct("<QIH")
_TRACK = struct.Struct("<IffffffB")
_SEGMENT = struct.Struct("<ffffI")


class Track:
    __slots__ = ("id", "x", "y", "heading", "speed", "length", "width", "cls")

    def __init__(self, values):
        self.id, self.x, self.y, self.heading, self.speed, self.length, self.width, self.cls = values


def parse_tracks(payload):
    _, n, stride = _TRACKS.unpack_from(payload, 0)
    return [Track(_TRACK.unpack_from(payload, _TRACKS.size + i * stride)) for i in range(n)]


def parse_color(c):
    c = c.lstrip("#")
    if len(c) == 6:
        return 0xFF000000 | int(c, 16)
    if len(c) == 8:
        return int(c, 16)  # aarrggbb, as in Qt
    raise ValueError(f"colour {c!r}")


class Overlay:
    """What a layer draws, in world metres (x right, y up)."""

    def __init__(self):
        self._segments = bytearray()
        self._count = 0
        self._labels = []
        self._rgba = 0xFFFFFFFF

    def color(self, c):
        self._rgba = parse_color(c)

    def line(self, x1, y1, x2, y2):
        self._segments += _SEGMENT.pack(x1, y1, x2, y2, self._rgba)
        self._count += 1

    def polygon(self, points):
        for (x1, y1), (x2, y2) in zip(points, points[1:] + points[:1]):
            self.line(x1, y1, x2, y2)

    def text(self, x, y, s):
        self._labels.append((x, y, self._rgba, s.encode()[:256]))

    def encode(self):
        out = bytearray(struct.pack("<IH", self._count, _SEGMENT.size))
        out += self._segments
        out += struct.pack("<I", len(self._labels))
        for x, y, rgba, b in self._labels:
            out += struct.pack("<HffIH", 14 + len(b), x, y, rgba, len(b)) + b
        return bytes(out)


def _read(f, n):
    b = f.read(n)
    if len(b) < n:
        raise EOFError
    return b


def _write(out, kind, version, t, payload):
    out.write(_RECORD.pack(kind, version, 0, 0, t, len(payload)))
    out.write(payload)


def run(update, name, api="1.0"):
    """Calls update(tracks, overlay, state) for every tracks record until the viewer goes away.
    state["selected"] is the selected object's id (-1: none), state["info"] the viewer's hello."""
    inp, out = sys.stdin.buffer, sys.stdout.buffer
    magic, major, _, size = _HELLO.unpack(_read(inp, _HELLO.size))
    if magic != MAGIC or major != 1:
        sys.exit("not a viewer")
    state = {"selected": -1, "info": json.loads(_read(inp, size))}
    info = json.dumps({"name": name, "api": api}).encode()
    out.write(_HELLO.pack(MAGIC, 1, 0, len(info)) + info)
    out.flush()
    try:
        while True:
            kind, version, _, flags, t, size = _RECORD.unpack(_read(inp, _RECORD.size))
            payload = _read(inp, size)
            if kind == SELECTION:
                state["selected"] = struct.unpack_from("<i", payload)[0]
            elif kind == TRACKS and not flags:
                overlay = Overlay()
                update(parse_tracks(payload), overlay, state)
                _write(out, OVERLAY, 1, t, overlay.encode())
                out.flush()
    except (EOFError, BrokenPipeError):
        pass
