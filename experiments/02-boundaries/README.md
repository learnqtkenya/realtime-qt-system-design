# Module 2: Who owns the data, and where are the boundaries?

> **Intermediate:** Module 1 says the point path is the problem. So we make the drawing faster?
>
> **Senior:** Look at the first four findings again: building a list, converting it to JavaScript, drawing it, collecting the garbage. Three of those exist only because the data *moved*. Before we touch drawing, decide who owns the data and what is allowed to cross into QML. Change that one thing, keep everything else the same, and measure.

## The decision

**C++ owns the data. QML gets handles, never copies.**

| | Module 1 | Module 2 |
|---|---|---|
| Owner of lidar frames | QML, a JS copy per sensor | `FrameStore` (C++), immutable shared frames |
| Crosses into QML per lidar frame | A `QVariantList` of every point | Nothing; the view item is told to update |
| Draws points | JS `Canvas` + its rasteriser | `PointCloudView` (`QQuickPaintedItem`) with `QPainter` |
| Tracks | `QVariantList` of `QVariantMap`, replaced | C++ list model, reset (same propagation) |
| Event log | Model reset per batch | Unchanged |

The point view still rasterises on the CPU with `QPainter`, which is what Canvas does internally. That keeps "where data lives" separate from "how pixels are made", which is Module 4's question.

### The layers

```
SensorSimulator ──signals──▶ FrameStore ◀──reads── PointCloudView   (QML item, C++)
   (ingest)                  (domain,             TrackListModel    (QML model, C++)
                              QtCore only)        EventLogModel
                                                        ▲
main() builds all of it, then hands the Scene  ─────────┘
singleton to the engine (setExternalSingletonInstance).
QML only arranges: PointCloudView { store: Scene.store }
```

> **Intermediate:** Why is `FrameStore` registered with `QML_FOREIGN` and not `QML_ELEMENT`?
>
> **Senior:** Because the store is domain code and shouldn't know QML exists. `QML_FOREIGN` lets the presentation layer give the store a QML name, uncreatable, without touching its header. If we swap QML for something else tomorrow, the domain doesn't change.

> **Intermediate:** And `setExternalSingletonInstance`?
>
> **Senior:** New in 6.12. `main()` is the composition root: it builds the store, the models and the simulator, wires them, and only then gives QML the one object it may see. No static `create()` reaching for globals. Combined with `QML_UNCREATABLE`, QML can't build its own copy by accident.

One threading rule appears for the first time. `QQuickPaintedItem::paint()` runs on the **render thread while the GUI thread is blocked** (Qt docs, verified in the 6.12 documentation). That makes reading the store from `paint()` safe. It also means painting time is GUI-blocking time, even though it isn't GUI *CPU* time.

## Predictions

Committed before the code existed: [`results/m02/predictions.md`](../../results/m02/predictions.md).

## Results

Full table: [`results/m02/summary.md`](../../results/m02/summary.md). Same machine and conditions as Module 1.

### Module 1 against Module 2

| Points / sensor | | fps | GUI CPU | GUI lag p99 | Sync p99 | Latency p50 / max | Backlog max | RSS max |
|---|---|---|---|---|---|---|---|---|
| 10k | M1 | 59.9 | 67% | 36 ms | – | 29 / 50 ms | 0 | 164 MB |
| | **M2** | **59.9** | **12%** | **7.2 ms** | 5.0 ms | 25 / 34 ms | 0 | 133 MB |
| 25k | M1 | 32.9 | 96% | 175 ms | – | 63 / 147 ms | 4 | 221 MB |
| | **M2** | **59.9** | **11%** | **35 ms** | 10 ms | 28 / 50 ms | 1 | 136 MB |
| 50k | M1 | 8.7 | 99% | 1,640 ms | – | 598 / 1,137 ms | 44 | 1.4 GB |
| | **M2** | **54.5** | **14%** | **74 ms** | 30 ms | 30 / 156 ms | 5 | 142 MB |
| 150k (nominal) | M1 | 0.7 | 100% | 9,407 ms | – | 4,360 / 7,979 ms | 356 | 5.9 GB, killed |
| | **M2** | **17.7** | **15%** | **285 ms** | 67 ms | 128 / 210 ms | 7 | 163 MB |

Tracks (Series B) and events (Series C) behave as before, as expected, since neither decision changed: about 36–68 µs per object per rebuild, event resets 60–95 ms/s, and event memory 21 MB/min at 2000 events/s.

## Predictions against measurements

| Prediction | Measured | |
|---|---|---|
| List build and JS conversion disappear | Gone | Right |
| `paint()` costs 100–160 ns/point | 66–106 ns/point | Cheaper: `QPainter` draws directly; Canvas recorded then replayed |
| GUI CPU at 10k: 12–20% | 11.8% | Right |
| Painting shows up as sync time and GUI lag, not GUI CPU | Sync p99 5 → 67 ms; lag p99 7 → 285 ms; GUI CPU stays 11–15% | Right |
| 60 fps up to 25–35k; 8–15 fps at 150k | 60 at 25k, 54.5 at 50k, 17.7 at 150k | Better than predicted (cheaper paint) |
| Backlog 0 at every load | ≤ 9 frames, bounded | Nearly: arrivals queue while the GUI thread is blocked in sync |
| Latency at 150k bounded at 100–250 ms | p50 128, max 210 ms, not growing | Right |
| RSS flat at 140–200 MB | 132–163 MB, flat | Right |

## What the numbers say

> **Intermediate:** 150k went from 0.7 fps to 17.7, and memory from 5.9 GB to 163 MB. We're done with points?
>
> **Senior:** Read the GUI columns together. GUI CPU is 15%, so by that number the UI thread is idle. But its event loop is late by up to 285 ms at p99. Both are true: the thread is *blocked*, not busy, waiting while the render thread paints 600,000 rectangles. Clicks, animations and timers all wait with it. A CPU percentage can't show blocking. That's why the harness measures lag.

What changed, in order of effect:

1. **Memory and backlog are under control.** RSS stays flat at every load and the backlog is bounded. Data no longer gets copied per frame, so nothing piles up. This was the boundary decision's main job.
2. **The GUI thread got its CPU back.** 67% → 12% at 10k, and about 100 ms/s saved even at the lightest load.
3. **The bottleneck moved to the render thread.** Paint costs ~90 ns/point. At 150k that's ~55 ms per frame, so 17.7 fps with one core saturated (process CPU 101%).
4. **The GUI thread now waits for that paint.** Lag p99: 7 ms at 10k, 35 ms at 25k, 285 ms at 150k. Moving work off the GUI thread's CPU isn't the same as moving it off the GUI thread's critical path.
5. **Every arrival still repaints all four sensors.** Paints per second still follow arrivals (40/s) until the render thread can't keep up.
6. **Unchanged:** event-log resets (~65 ms/s), track rebuilds (~36–68 µs per object), unbounded event memory.

## The harness learned two more things

- **Render-thread latency marks.** The latency probe is now a thread-safe free function (`qp::markContentCaptured`), because content is now produced on the render thread.
- **Presentation pauses that aren't ours.** One run showed fps 32.5 while every frame interval looked normal and the GUI was 84% idle. Presentation had stopped for seconds at a time, and the gap moved to a different run on a re-run, regardless of load. That is the compositor pausing a covered window, not the app. The report now includes `presentationPauseMs` and flags `suspectExternalPause` when presentation stopped for over a second while the GUI thread stayed responsive. The sweep retries such runs. All Module 1 runs were checked retrospectively and are clean.

> **Intermediate:** So what's next: threads, or the GPU?
>
> **Senior:** Both questions are now visible in the data. Module 3: how should work flow so that a slow stage never blocks the GUI thread, and what do we do when frames arrive faster than we can use them? Module 4: should the CPU be producing these pixels at all?

## Run it

```bash
./scripts/check.sh
python3 scripts/sweep.py m02                                   # ~7 min, opens windows
build/release/experiments/02-boundaries/m02_boundaries --points 50000
```
