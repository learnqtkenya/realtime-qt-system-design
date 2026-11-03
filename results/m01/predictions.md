# Module 1: predictions (written before the sweep)

Inputs from Module 0: building a `QVariantList` of `QVector3D` costs ~25 ns/point; the display is 60 Hz; the GUI-thread budget is 4 ms per frame (~24% of the thread).

The rest are guesses.

| Stage | Guess | Basis |
|---|---|---|
| `convert_points_ms` (C++ list build) | 25 ns/point | Measured in Module 0 |
| `lidar_signal_ms` (list → JS array + handler) | 100–300 ns/point | One JS value-type wrapper per point |
| `canvas_paint_ms` | 0.3–1 µs/point drawn; each paint draws all 4 sensors | Property reads plus a `fillRect` call per point |
| `tracks_rebuild_ms` | 30–80 µs per object | Item + Rectangle + Text per object, recreated each update |
| `event_reset_ms` | 0.2–1 ms per batch | Only the visible rows are rebuilt |
| Event memory | 100–200 B per event retained | `QString`s plus the record |

## What those guesses imply

With 4 sensors at 10 Hz and `p` points per sensor frame:

- GUI time per second ≈ 40·p·(25 + 200) ns for arrival, plus up to 40 paints/s × 4p × 0.5 µs.
  That is about p × 89 µs per second.
- **GUI thread exceeds its 24% budget** at p ≈ 2.7k points per sensor.
- **GUI thread saturates** (frame rate collapses) at p ≈ 11k.
- Paints coalesce when the thread is busy, so arrival work alone decides whether the backlog grows: 40·p·225 ns = 1 s at **p ≈ 110k**. Below that, latency should stay bounded even when frames are slow; above it, backlog and latency grow for the whole run.
- **Tracks:** 400 objects ≈ 12–32 ms per update at 10 Hz, so 12–32% of the GUI thread from tracks alone. Expect visible trouble at 400–800 objects.
- **Events:** no frame-time effect up to 2000 events/s; RSS growth of roughly 12–24 MB/min at 2000 events/s.

## Sweep

| Series | Varies | Fixed |
|---|---|---|
| A | points per sensor: 1k, 5k, 10k, 25k, 50k, 100k, 150k | 4 sensors @ 10 Hz, 50 objects, 50 events/s |
| B | objects: 50, 200, 400, 800 | 1k points, 50 events/s |
| C | events/s: 50, 500, 2000 (30 s runs) | 1k points, 50 objects |
| Control | simulator off | |
