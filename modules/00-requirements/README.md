# Module 0: How big is the problem?

> **Intermediate:** Can we start on the 3D view?
>
> **Senior:** Not yet. Without numbers, "it feels slow" and "it feels fine" are both just opinions. First we decide what "fast enough" means, estimate how much data is moving, and build something that measures it. Then every later change has a before and an after.

## What we built

| Piece | Where | Purpose |
|---|---|---|
| Sensor simulator | `common/src/SensorSimulator.cpp` | Lidar, tracks and events on its own thread, deterministic per seed |
| Frame monitor | `common/src/FrameMonitor.cpp` | Frame intervals, sync / render time, GUI event-loop lag, CPU, RSS, sensor-to-pixel latency |
| Stage timers | `common/src/StageStats.cpp` | Named timings any thread (or QML) can record |
| HUD | `common/qml/Hud.qml` | The monitor's last second, on screen |
| `m00_sim_bench` | `src/sim_bench_main.cpp` | Headless: generation cost, memcpy bandwidth, QVariant cost, delivery schedule |
| `m00_harness_check` | `qml/Main.qml` | A trivial animated scene, to see what "healthy" looks like |
| Estimates | `scripts/estimates.py` → `results/m00/estimates.md` | Written and committed **before** measuring |

> **Intermediate:** Why is the benchmark driven by its own thread?
>
> **Senior:** Because the thing we are measuring might freeze the GUI thread. If a GUI timer ends the run, a frozen app never writes its report, and the worst result goes missing. The watchdog thread writes the report regardless and forces an exit if the app cannot quit.

> **Intermediate:** And event-loop lag?
>
> **Senior:** A 2 ms timer on the GUI thread. When it fires late, something held the thread for that long. It is the cheapest honest answer to "how busy is the GUI thread?"

## Estimates first

Full arithmetic is in `results/m00/estimates.md`. The headline:

| Case | Points/s | Bandwidth (xyz float) |
|---|---|---|
| Nominal: 4 × 150k pts @ 10 Hz | 6 M | 72 MB/s |
| Peak: 4 × 300k pts @ 20 Hz | 24 M | 288 MB/s |

> **Intermediate:** The plan said 1–5M points per second.
>
> **Senior:** It did, and the plan's own sensor numbers say 4–24M. Doing the multiplication caught it. That is what estimates are for.

## Measured against the predictions

Hardware: Ryzen 9 7940HS, Radeon 780M (integrated, RADV / radeonsi, Mesa 26.0.8), Wayland, 60 Hz display, CPU governor `powersave`, normal desktop running. Release build (`RelWithDebInfo`).

| Quantity | Predicted | Measured | Verdict |
|---|---|---|---|
| Cloud generation | 20–60 ns/point | 18–19 ns/point | Slightly faster than predicted |
| Single-thread memcpy | 10–25 GB/s | 17.8 GB/s (median) | Within range |
| `QVariantList` of `QVector3D` | 30–80 ns/point | 22–27 ns/point | Faster: wrong reasoning, see below |
| Simulator delivery, nominal | ≥ 99% of frames, jitter < 5 ms | 98.5% in a 10 s window; steady-state interval 100.000 ms, jitter 0.71 ms | Rate and jitter met; the shortfall is start-up |
| Idle frame interval | p99 within 1 ms of median | OpenGL: median 16.68, p99 17.69 ms (1.01 ms). Vulkan: p99 17.91 ms | On the line |
| Idle event-loop lag | p99 < 1 ms | 0.47 ms (max 2.3 ms) | Met |

Raw reports: `results/m00/sim_bench.json`, `results/m00/harness_idle_default.json`, `results/m00/harness_idle_vulkan.json`.

> **Intermediate:** You got the QVariant cost wrong.
>
> **Senior:** I assumed a heap allocation per point. Qt 6's `QVariant` is 32 bytes and stores small types inline; a 12-byte `QVector3D` never touches the heap (checked: `data_ptr().is_shared == 0`, while a `QMatrix4x4` is heap-allocated). Converting 150k points still costs about 4 ms per frame, which is the entire GUI-thread budget. Cheaper than I thought is not the same as cheap.

> **Intermediate:** And the 98.5%?
>
> **Senior:** The simulator generates its point pools and staggers sensor start times before the first frame. Over a 10 s window that costs about one or two frames per sensor. The mean steady-state interval is 100.000 ms, so nothing is dropped once it is running. The lesson is about measurement windows, not about the simulator.

> **Intermediate:** The idle scene is already 1 ms away from the budget at p99.
>
> **Senior:** Frame *intervals* on a 60 Hz display are quantised to vsync. 16.68 ms is the display, not our code. That is why "slow" is defined as an interval above 20 ms: a frame that missed vsync, not one that jittered.

## Decisions

| Decision | Reason |
|---|---|
| Service level: frame intervals > 20 ms count as missed frames | The display already sits at 16.68 ms; jitter below 20 ms is not a missed vsync |
| GUI-thread budget is judged by event-loop lag and GUI CPU %, not a per-frame timer | Both are measurable from outside the code under test |
| Nominal load is 6M points/s, peak 24M | Corrects the plan; follows from the sensor parameters |
| Default graphics API stays as Qt's default (OpenGL here); `--api` switches it | Vulkan is available and works; the choice is evaluated in Module 4 |
| Every module is measured with the release preset | Debug builds measure the compiler, not the design |

## Run it

```bash
cmake --preset release && cmake --build --preset release && ctest --preset release
python3 modules/00-requirements/scripts/estimates.py > results/m00/estimates.md
build/release/modules/00-requirements/m00_sim_bench --duration 10 --out results/m00/sim_bench.json
build/release/modules/00-requirements/m00_harness_check --bench --warmup 3 --duration 15 --out results/m00/harness_idle_default.json
```
