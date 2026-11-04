import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sdk", "python"))
import qpwire  # noqa: E402


def inside(x, y, poly):
    hit = False
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            hit = not hit
    return hit


def update(tracks, draw, state):
    props = state["info"].get("properties", {})
    zone = [tuple(p) for p in props.get("zone", [])]
    if len(zone) < 3:
        return
    inn = [t for t in tracks if inside(t.x, t.y, zone)]
    draw.color("#F472B6" if inn else "#A78BFA")
    draw.polygon(zone)
    top = max(zone, key=lambda p: p[1])
    draw.text(top[0], top[1] + 2, f"{props.get('label', 'Zone')}: {len(inn)} inside")
    draw.color("#F472B6")
    for t in inn[:50]:
        r = max(t.length, t.width) * 0.75
        draw.polygon([(t.x - r, t.y - r), (t.x + r, t.y - r), (t.x + r, t.y + r), (t.x - r, t.y + r)])


qpwire.run(update, "geofence")
