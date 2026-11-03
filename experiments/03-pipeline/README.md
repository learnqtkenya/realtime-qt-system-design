# Module 3: How should work flow when there's more of it than we can handle?

> **Intermediate:** Module 2 moved painting to the render thread and the GUI thread still waits for it. Can we just move painting to another thread?
>
> **Senior:** Yes, but that's only half the question. Once work runs on its own thread, it can fall behind on its own. Then something has to decide what happens to a frame that arrives while the worker is busy: wait, throw it away, or throw an older one away. Today that decision is made by accident, by an unbounded Qt event queue. We'll make it on purpose.

## The decision

**Lidar work flows through per-sensor lanes with an explicit overload policy. Worker threads rasterise; nothing the GUI thread does waits for them.**

| | Module 2 | Module 3 |
|---|---|---|
| Lidar path | Simulator → GUI thread → store → render thread paints all sensors while the GUI thread waits | Simulator thread → per-sensor lane → worker rasterises **one sensor** into its own `QImage` → render thread uploads changed layers only |
| Work per lidar frame | Repaint 4 sensors | Rasterise 1 sensor |
| When the worker is behind | Qt event queue, unbounded | Lane with a depth and a policy: `block`, `drop-newest`, `drop-oldest` |
| Pixels | `QPainter` | `QPainter` (same per-point cost; the GPU question is Module 4) |

```
                         lane 0 [depth N, policy] ─┐
SensorSimulator thread ─▶ lane 1                   ├─▶ worker threads ─▶ layer per sensor ─▶ render thread
   (direct call,          lane 2                   │    (rasterise       (latest QImage,      (uploads changed
    never the GUI thread) lane 3                  ─┘     one frame)       serial number)       layers as textures)
```

Rules that keep this correct, all covered by unit tests (`tests/tst_pipeline.cpp`, 30 consecutive passes):

- A lane is never processed by two workers at once, so each sensor's frames stay in order.
- Lanes are served round-robin, so one sensor can't starve the others.
- `block` waits on a condition variable that `shutdown()` releases. The application shuts the pipeline down *before* stopping the simulator thread, or a blocked producer would deadlock the exit.
- `drop-oldest` with depth 1 is "latest value wins", the default.

> **Intermediate:** Why a lane per sensor and not one queue?
>
> **Senior:** Two reasons. Module 2 repainted all four sensors whenever any one changed: four times the work for the same information. And a single queue makes a slow sensor delay the fast ones. Partition the work the way the data is already partitioned.

## Predictions

Committed before the code: [`results/m03/predictions.md`](../../results/m03/predictions.md).

## Results

Full table: [`results/m03/summary.md`](../../results/m03/summary.md). Same machine and conditions as before. All 20 runs passed the external-pause check on the first attempt.

### Default (latest value per sensor, 4 workers) against Module 2

| Points / sensor | | fps | GUI lag p99 | Sync p99 | Latency p50 / p99 | Process CPU | RSS max |
|---|---|---|---|---|---|---|---|
| 50k | M2 | 54.5 | 74 ms | 30 ms | 30 / – ms | 75% | 142 MB |
| | **M3** | **59.9** | **5.2 ms** | **1.5 ms** | 29 / 37 ms | 36% | 167 MB |
| 150k (nominal) | M2 | 17.7 | 285 ms | 67 ms | 128 / – ms | 101% | 163 MB |
| | **M3** | **59.9** | **5.4 ms** | **1.5 ms** | **34 / 43 ms** | 60% | 186 MB |

Every Series A load from 1k to 150k now runs at 59.9 fps with GUI lag p99 between 5.1 and 5.4 ms. Rasterising one sensor frame of 150k points takes 9.8 ms (≈ 65 ns/point) on a worker, and all 40 frames per second reach the screen.

### Overload: Series P

Peak load: 4 sensors × 300k points at 20 Hz = 80 frames/s, 16 ms to rasterise each. One worker can do ≈ 62 frames/s, so demand is 1.28× capacity.

| Policy (depth), workers | Produced /s | Dropped /s | Rasterised /s | Queue wait p50 | Latency p50 / p99 | GUI lag p99 |
|---|---|---|---|---|---|---|
| `block` (4), 1 | **62.6** (source slowed) | 0 | 62.6 | 235 ms | 274 / 385 ms | 4.4 ms |
| `drop-newest` (4), 1 | 80 | 16.6 | 63.5 | 227 ms | 266 / 304 ms | 4.8 ms |
| `drop-oldest` (4), 1 | 80 | 17.8 | 62.3 | 175 ms | 215 / 245 ms | 4.8 ms |
| `drop-oldest` (1), 1 | 80 | 17.4 | 62.6 | 25 ms | **65 / 94 ms** | 4.9 ms |
| `drop-oldest` (1), 4 | 80 | 0 | **80** | 0 ms | **40 / 49 ms** | 5.4 ms |

> **Intermediate:** The deep queues all deliver the same 62 frames a second as the shallow one.
>
> **Senior:** That's the whole lesson. Under *sustained* overload, a queue doesn't add capacity; it adds waiting. Every policy with one worker rasterises about 62 frames a second. Depth 4 just means each frame waits behind more frames: up to depth × lanes × service time = 4 × 4 × 16 ms ≈ 256 ms, which is what `block` and `drop-newest` show. A queue is for *bursts*. For sustained overload you have two levers: shed load (depth 1) or add capacity (4 workers).

> **Intermediate:** And `block` never drops anything. Isn't that the safest?
>
> **Senior:** It moves the drop somewhere you can't see it. The simulator thread spent 998 ms of every second blocked, and it produced 62.6 frames/s instead of 80. Those frames weren't dropped by us; they were never produced. A real lidar doesn't wait; its driver drops packets or its buffer overflows. And the latency column flatters `block`, because a frame's capture time is stamped when the blocked producer finally gets to run. Blocking only makes sense when the source can genuinely slow down, like a file reader.

`drop-oldest` (4) beats `drop-newest` (4) on latency (215 vs 266 ms) at the same throughput. Evicting the oldest keeps the waiting frames younger.

## Predictions against measurements

| Prediction | Measured | |
|---|---|---|
| 60 fps at every load | 59.9 at every load | Right |
| GUI lag p99 5–8 ms | 5.1–5.4 ms | Right |
| Sync p99 ≤ 5 ms | 1.5–1.7 ms | Right |
| Rasterise at 150k: 10–15 ms | 9.8 ms | Right, at the low end |
| 40 content updates/s | 40.0 | Right |
| Latency p50 at 150k: 30–45 ms | 34.4 ms | Right |
| Process CPU at 150k: 55–80% | 60% | Right |
| RSS 170–210 MB | 159–186 MB, flat over 120 s | Right |
| One worker manages ≈ 37 frames/s at peak | 62 frames/s (16 ms, not 27) | Wrong: per-point cost at 300k is lower |
| Deep-queue latency 110–180 ms | 215–274 ms | Wrong: I forgot one worker serves four lanes; depth × lanes × service fits |
| Depth 1 latency 30–80 ms | 65 ms | Right |
| 4 workers: no drops, 35–60 ms | 0 drops, 40 ms | Right |
| **Depth adds latency, not throughput** | Throughput 62.3–63.5/s for every 1-worker policy | **Confirmed** |

## Checks on the measuring itself

- **Memory slope.** The 15-second runs report ~13.6 MB/min RSS growth, against ~1 MB/min in Module 2. A 120-second soak at 150k (`results/m03/diag/soak-150k-120s.json`) shows RSS rising from 182 to 187 MB and then flattening: 1.3 MB/min overall, median 186 MB. It's warm-up (allocator arenas for four worker threads, texture churn), not a leak. Reports now include the full RSS series (`rssSeriesMb`), so plateaus can be seen directly. Short runs are not evidence about memory.
- **`upload_ms` measures the wrong thing.** It reads 0.01 ms, because `createTextureFromImage()` only wraps the image; the pixel transfer happens later in the render pass. Render-thread CPU per frame went from 0.04 ms (control) to ≈ 0.3 ms, so handing 40 × 3.3 MB/s to the GPU is cheap *on the CPU*. GPU-side cost is invisible to this harness, a gap to close in Module 4.

## What the numbers say now

1. **The GUI thread is out of the lidar path.** Lag p99 is 5.1–5.4 ms at every point load, and it doesn't move with load anymore.
2. **The GUI budget is still missed, and now the cause is plain.** 5.2 ms > 4 ms. The remaining long tasks are the event-log reset (~3.3 ms each, 20/s) and the track rebuild (~2.7 ms at 50 objects). Series B: 800 objects still cost 52.6 fps and 44 ms lag. That's Module 7's question.
3. **Rasterising on the CPU works, but it isn't free.** 60% of a core at nominal, 164% at peak with 4 workers, for 1.5-pixel squares. Module 4 asks whether the CPU should make these pixels at all.
4. **Overload is a policy now, not an accident.** The default (latest value, depth 1) keeps latency bounded under overload by dropping stale frames, and the drop rate is measured.

> **Intermediate:** So we keep 4 workers and depth 1 everywhere?
>
> **Senior:** For a live view, yes: stale lidar is worthless, so drop it. But notice what we just wrote down: the *requirement* chose the policy. A recorder (Module 9) must not drop anything, so it'll need a different answer to the same question.

## Run it

```bash
./scripts/check.sh
python3 scripts/sweep.py m03                                  # ~10 min, opens windows
build/release/experiments/03-pipeline/m03_pipeline --points 300000 --rate 20 --workers 1 --policy block --queue 4
```
