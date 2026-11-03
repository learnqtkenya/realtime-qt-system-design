# Module 2: predictions (written before the code exists)

## The change

| | Module 1 | Module 2 |
|---|---|---|
| Who owns lidar frames | QML (a JS copy per sensor) | C++ `FrameStore` (shared, immutable frames) |
| What crosses into QML per lidar frame | A `QVariantList` of every point | Nothing; the view item is told to update |
| Who draws points | JS `Canvas.onPaint` + Canvas rasteriser, GUI thread | C++ `QQuickPaintedItem::paint()` with `QPainter`, render thread **while the GUI thread is blocked** |
| Tracks | `QVariantList` of `QVariantMap`, whole list replaced | C++ list model, whole model reset (propagation unchanged) |
| Event log | C++ model, reset per batch | Unchanged |

## Inputs from Module 1

- Canvas rasterising: ≈ 248 ms/s for 40 paints × 40k points at 10k/sensor ≈ 155 ns per point.
- Fixed GUI costs at light load: event resets ≈ 60 ms/s, track rebuilds ≈ 30 ms/s at 50 objects.

## Guesses

| Quantity | Module 1 | Prediction |
|---|---|---|
| List build + JS conversion | 25–50 + 200–320 ns/point | 0 |
| `paint()` cost | (JS 100–240 + raster ≈ 155 ns/point) | 100–160 ns/point drawn |
| GUI CPU at 10k points | 67% | 12–20% (only fixed costs remain) |
| Where painting time shows up | GUI CPU | Scene-graph **sync** time and GUI event-loop lag, because the GUI thread is blocked during `paint()` |
| Sync time per frame at 10k | ≈ 0 | 4–7 ms (40k points × 100–160 ns) |
| Frame rate | 60 to 10k, 33 at 25k, 0.7 at 150k | 60 up to ≈ 25–35k; ≈ 8–15 fps at 150k (600k points × 100–160 ns ≈ 60–100 ms per paint) |
| Backlog | 356 frames at 150k | 0 at every load (arrival is a pointer store) |
| Latency at 150k | 4.4 s p50, growing | Bounded: 100–250 ms, not growing |
| RSS | 5.9 GB at 150k, growing | Flat, 140–200 MB at every load |
| Track rebuild per object | 44–60 µs | About the same (35–60 µs): the Repeater still rebuilds every delegate |
| GUI lag p99 at 1k | 7.4 ms | About the same (fixed costs unchanged) |

## What would prove this wrong

- GUI CPU staying high at 10k would mean the cost was not mostly the data path.
- RSS still growing would mean the memory was not the JS copies.
- Frame rate at 150k well below 8 fps would mean `QPainter` is slower per point than the Canvas rasteriser was.
