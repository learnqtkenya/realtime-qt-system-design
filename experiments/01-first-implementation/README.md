# Module 1: Where does a straightforward design break first, and why?

> **Intermediate:** I'll build it the way the Qt examples do it. C++ receives the data, hands it to QML, QML draws it.
>
> **Senior:** Good. Build it exactly like that, then we turn the load up until something gives. We have a budget from Module 0, so we'll know when it gives, and the stage timers will tell us where.

## What we built

`m01_first` shows a top-down view of four fused lidar sensors, the tracked objects, and a live event log.

| Part | How it works |
|---|---|
| `SensorBridge` (C++, QML singleton) | Receives simulator signals on the GUI thread. Each lidar frame becomes a `QVariantList` of `QVector3D` and is emitted to QML. Tracks become a `QVariantList` of `QVariantMap`. |
| Point view | A `Canvas`. Each sensor's latest list is stored in a JS array; `onPaint` draws every point of every sensor with `fillRect`. |
| Tracks | A `Repeater` over the track list: a `Rectangle` and a `Text` per object. |
| Event log | `EventLogModel` (`QAbstractListModel`) shown in a `ListView`. Each batch is appended inside a model reset. |
| Instrumentation | Stage timers around each step, plus the Module 0 harness. |

## Predictions

Written and committed before the first run: [`results/m01/predictions.md`](../../results/m01/predictions.md).

## First lesson: the harness lied

> **Intermediate:** The first sweep said even the lightest load drops 200 frames.
>
> **Senior:** Look at the control run. Simulator off, nothing moving, and it reports 1 fps. Qt Quick doesn't render a scene that hasn't changed, so the gaps between frames were idle time, not misses. And in the heavy runs the CPU sampler ran on the very GUI thread that was stuck, so it got one sample in fifteen seconds.

Two fixes went into the harness (v2, `common/`):

- CPU and RSS are sampled on their own thread, reading the GUI thread's CPU clock from outside (`pthread_getcpuclockid`).
- The HUD has a heartbeat animation, so the scene asks for a frame every vsync and a long interval is a real miss.

After the fixes the control run shows 60 fps with zero misses. The first sweep is kept in `results/m01/harness-v1/` with a note. **Measure the measuring tool before you trust it.**

## Results (harness v2)

Full table: [`results/m01/summary.md`](../../results/m01/summary.md). Raw reports: `results/m01/raw/`. Release build, 60 Hz display, OpenGL, `powersave` governor, normal desktop running.

### Series A: points per sensor (4 sensors @ 10 Hz, 50 objects, 50 events/s)

| Points / sensor | fps | Frame p99 (ms) | Frames > 20 ms | GUI lag p99 (ms) | GUI CPU | Latency p50 / max (ms) | Backlog max | RSS max |
|---|---|---|---|---|---|---|---|---|
| 1k | 60.0 | 17.5 | 0 | 7.4 | 22% | 25 / 35 | 0 | 141 MB |
| 5k | 59.9 | 17.8 | 1 | 13.5 | 44% | 25 / 35 | 0 | 151 MB |
| 10k | 59.9 | 20.5 | 17 | 36 | 67% | 29 / 50 | 0 | 164 MB |
| 25k | 32.9 | 56 | 350 | 175 | 96% | 63 / 147 | 4 | 221 MB |
| 50k | 8.7 | 305 | 110 | 1,640 | 99% | 598 / 1,137 | 44 | 1.4 GB |
| 100k | 0.9 | 3,275 | 8 | 4,465 | 100% | 2,028 / 3,542 | 140 | 5.3 GB |
| 150k (nominal) | 0.7 | 4,351 | 9 | 9,407 | 100% | 4,360 / 7,979 | 356 | 5.9 GB, forced exit |

### Series B: tracked objects (1k points)

| Objects | fps | Frame p99 | Frames > 20 ms | GUI lag p99 | GUI CPU | Track rebuild per update |
|---|---|---|---|---|---|---|
| 50 | 59.9 | 18.0 | 0 | 7.8 | 23% | 3.0 ms |
| 200 | 59.9 | 18.0 | 1 | 17.4 | 30% | 9.6 ms |
| 400 | 60.0 | 28.1 | 106 | 29.5 | 39% | 17.4 ms |
| 800 | 47.8 | 53.7 | 162 | 56.4 | 58% | 35.1 ms |

### Series C: events per second (1k points, 30 s runs)

No effect on frames at any rate (p99 ≤ 18 ms). RSS slope: 0.8 MB/min at 50/s, 10.2 at 500/s, **20.0 at 2000/s**.

## Predictions against measurements

| Prediction | Measured | |
|---|---|---|
| GUI exceeds its 24% share at ~2.7k points | 22% at 1k, 44% at 5k | Earlier: the model ignored fixed costs (event log, tracks) |
| GUI saturates at ~11k points | 67% at 10k, 96% at 25k | Right range |
| Backlog grows only beyond ~110k | Backlog 44 and 0.6 s latency at 50k | Wrong: the model assumed painting stops when busy; it doesn't |
| List → JS: 100–300 ns/point | 200–320 ns/point | Right, at the top end |
| Canvas: 0.3–1 µs/point | 100–240 ns/point in `onPaint`, **plus** about as much again after it returns | Half right; see below |
| Tracks: 30–80 µs/object | 44–60 µs/object | Right |
| Event reset: 0.2–1 ms/batch | **3.3 ms** mean, 5.4 ms p99, regardless of rate | Wrong by 3–6× |
| Events retain 100–200 B each | ≈ 160 B/event | Right |

## Following the unexplained time

> **Intermediate:** At 10k points the stage timers add up to 373 ms per second, but the GUI thread is busy for 672. Where's the rest?
>
> **Senior:** My first guess was the JavaScript garbage collector. So I asked it: `QT_LOGGING_RULES="qt.qml.gc.*=true"` logs every incremental GC step. It came to about 27 ms per second, so my guess was wrong.
>
> Second guess: `onPaint` only *records* drawing commands, and the Canvas rasterises them after the handler returns. Timing from paint start to the `painted` signal gives 430 ms/s against 182 ms/s inside `onPaint`. That accounts for most of the gap.

| At 10k points | ms of GUI time per second |
|---|---|
| Canvas: `onPaint` (JS) | 182 |
| Canvas: rasterising after `onPaint` | ≈ 248 |
| List → JS conversion on signal delivery (`lidar_signal`) | 92 |
| Event log resets | 57 |
| Track rebuilds | 28 |
| JS garbage collection | ≈ 27 |
| Building the `QVariantList` in C++ | 13 |

The JS handler body itself costs 0.014 ms per call. Almost all of `lidar_signal` is the argument conversion that happens before the handler runs.

At 50k points the JS heap reaches **1.1 GB** (16 MB at 10k), while GC runs only ~51 ms/s. RSS growth beyond 25k points is JS heap: point arrays are created faster than a saturated thread can collect them. Probe reports: `results/m01/diag/`.

## What the numbers say, ranked

1. **Everything per point runs on the GUI thread, several times.** Build a list (25–50 ns), convert it to JS (200–320 ns), draw it (100–240 ns), then rasterise it (about the same again). The GUI thread saturates between 10k and 25k points per sensor. Nominal load is 150k.
2. **Memory follows the load and doesn't come back.** 16 MB of JS heap at 10k, 1.1 GB at 50k, 5.9 GB RSS at 150k, where the app had to be force-killed.
3. **Nothing limits how far behind the screen may fall.** At 150k the backlog reaches 356 frames and the picture is 4–8 seconds old while looking live.
4. **Work is repeated.** Paints per second equal frame arrivals (40/s), and each paint redraws all four sensors, although each sensor only changes 10 times a second.
5. **Fixed costs arrive as long single tasks.** An event-log reset (3.3 ms) and a track rebuild (3 ms at 50 objects) each eat most of the 4 ms budget. That's why GUI lag p99 is 7.4 ms even at the lightest load.
6. **Tracks scale linearly with objects**, about 45 µs each per update. 800 objects take 35% of the GUI thread by themselves.
7. **The event log never forgets.** About 160 B per event, forever: 20 MB/min at 2000 events/s.

Latency floor: at light load the screen shows data 25 ms (p50) after capture, about one and a half frames.

> **Intermediate:** So which do we fix first?
>
> **Senior:** None of them as a fix. Look at items 1 to 4: they share a cause. Point data lives in the wrong place, and the GUI thread is the only worker. Module 2 asks who should own the data. Module 3 asks how work should flow when there's more of it than we can handle. The rest wait their turn.

## Looking at it

`scripts/vdesk.sh` starts a virtual X display (Xvfb) that can be screenshotted and clicked. It renders in software, so it's for looking, not measuring.

Observations from looking:

- Track boxes don't sit on point clusters, because the simulator moves tracks independently of the pillars in the cloud. A real perception stack would not do that. This is a simulator gap to close before the overlay work (Module 6).
- Labels overlap where objects bunch up.
- There is nothing to interact with yet. Interaction arrives with time control (Module 8).

## Run it

```bash
./scripts/check.sh
python3 scripts/sweep.py m01                                         # ~7 min, opens windows
build/release/experiments/01-first-implementation/m01_first --points 5000   # look at it
QT_LOGGING_RULES="qt.qml.gc.stepExecution=true;qt.qml.gc.allocatorStats=true" \
  build/release/experiments/01-first-implementation/m01_first --bench --duration 8 --points 50000 --out /tmp/gc.json
```
