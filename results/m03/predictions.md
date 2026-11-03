# Module 3: predictions (written before the code exists)

## The change

| | Module 2 | Module 3 |
|---|---|---|
| Lidar path | Simulator → GUI thread → store; render thread paints all sensors while the GUI thread waits | Simulator thread → per-sensor lane → worker threads rasterise **one sensor** into its own image → render thread uploads changed layers only |
| Who waits for rasterising | GUI thread (blocked in sync) | Nobody but the workers |
| Work per lidar frame | Repaint all 4 sensors | Rasterise 1 sensor |
| Overload policy | None (Qt event queue, unbounded) | Explicit: `block`, `drop-newest`, `drop-oldest`, with a queue depth |
| Rasteriser | `QPainter` on the scene graph's image | `QPainter` on a `QImage` per sensor (same cost per point) |

Tracks and the event log are unchanged.

## Inputs from Module 2

- `QPainter` rasterising ≈ 70–100 ns/point.
- Fixed GUI costs: event resets ≈ 65 ms/s, track rebuilds ≈ 3 ms per update at 50 objects.

## Guesses: default configuration (latest frame per sensor, 4 workers), Series A

| Quantity | Module 2 at 150k | Prediction at 150k |
|---|---|---|
| fps | 17.7 | 60 at every load |
| GUI lag p99 | 285 ms | 5–8 ms at every load (fixed costs only) |
| Sync p99 | 67 ms | ≤ 5 ms (texture uploads only) |
| Rasterise one sensor frame | — | 10–15 ms (150k × 70–100 ns) |
| Content updates per second | ≈ 15 paints | 40 (every lidar frame shown) |
| Latency p50 | 128 ms | 30–45 ms (raster + wait for sync + present) |
| Process CPU | 101% | 55–80% |
| RSS | 163 MB | 170–210 MB (layer images in flight) |

## Guesses: overload (Series P)

Peak load: 4 sensors × 300k points at 20 Hz = 80 frames/s. One worker. At ≈ 27 ms per frame, one worker can rasterise ≈ 37 frames/s, so demand is about 2.2× capacity.

| Policy (queue depth) | Throughput | Dropped | Latency p50 | Source |
|---|---|---|---|---|
| `block` (4) | ≈ 37 frames/s | 0 | 120–180 ms | Slowed to ≈ 37 of 80 frames/s |
| `drop-newest` (4) | ≈ 37 | ≈ 43/s | 110–160 ms, showing the oldest accepted data | Full rate |
| `drop-oldest` (4) | ≈ 37 | ≈ 43/s | 110–160 ms | Full rate |
| `drop-oldest` (1), "latest" | ≈ 37 | ≈ 43/s | 30–80 ms | Full rate |
| Latest, 4 workers | ≈ 80 (no overload) | ≈ 0 | 35–60 ms | Full rate |

**The claim being tested:** under sustained overload, a queue deeper than 1 adds latency of roughly depth × service time and buys no throughput. Queues only absorb bursts. Under sustained overload, the only levers are adding capacity or shedding load.

In every policy the GUI thread should stay unaffected (lag p99 ≤ 8 ms), because nothing it does waits for the workers.
