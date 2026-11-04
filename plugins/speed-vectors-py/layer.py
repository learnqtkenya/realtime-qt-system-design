import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sdk", "python"))
import qpwire  # noqa: E402


def update(tracks, draw, state):
    for t in tracks:
        draw.color("#34D399" if t.speed < 5 else "#FBBF24" if t.speed < 12 else "#F87171")
        draw.line(t.x, t.y, t.x + 2 * t.speed * math.cos(t.heading), t.y + 2 * t.speed * math.sin(t.heading))


qpwire.run(update, "speed vectors")
