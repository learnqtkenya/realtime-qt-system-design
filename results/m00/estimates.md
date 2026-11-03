# Module 0: capacity estimates (written before measuring)

## A. Lidar data volume

| Case | Sensors | Points/frame | Hz | Points/s | Bytes/frame | Bandwidth |
|---|---|---|---|---|---|---|
| nominal | 4 | 150,000 | 10 | 6.0 M | 1.8 MB | 72 MB/s |
| peak | 4 | 300,000 | 20 | 24.0 M | 3.6 MB | 288 MB/s |

**Correction to PLAN.md.** The plan said "about 1-5M points/s". With 4 sensors of 100k-300k points at 10-20 Hz the real range is 4M-24M points/s. Nominal is now 6M points/s (72 MB/s) and peak is 24M points/s (288 MB/s).

## B. Video volume (needed from Module 5)

| Format | Bytes/frame | One stream | 6 streams |
|---|---|---|---|
| NV12 (decoder output) | 3.11 MB | 93 MB/s | 0.56 GB/s |
| RGBA8 (after conversion) | 8.29 MB | 249 MB/s | 1.49 GB/s |

Every extra copy of the 6-stream RGBA path costs the following, counting a read and a write:

| Copies | Memory traffic |
|---|---|
| 1 | 3.0 GB/s |
| 2 | 6.0 GB/s |
| 3 | 9.0 GB/s |
| 4 | 11.9 GB/s |

## C. Frame time budget at 60 fps (16.67 ms)

Proposed allocation. Module 0 turns these into measured pass/fail limits.

| Stage | Thread | Budget |
|---|---|---|
| Input, bindings, animations, model updates | GUI | <= 4.0 ms |
| Scene-graph sync (GUI thread blocked) | GUI + render | <= 1.5 ms |
| Render recording and submit | render | <= 6.0 ms |
| GPU execution | GPU | <= 12.0 ms (overlaps CPU work) |
| Headroom for the compositor and jitter | - | >= 3.0 ms |

## D. Predictions for the Module 0 measurements

These are guesses made before looking at any number. The README records the measured values next to them.

| Quantity | Prediction | Reasoning |
|---|---|---|
| Cloud generation cost | 20-60 ns per point | One sin, one cos and a few RNG calls per point; scalar code, no SIMD |
| Single-thread memcpy bandwidth | 10-25 GB/s | Laptop-class DDR5, one core; the memory controller limit is far higher |
| QVariantList of QVector3D, build cost | 30-80 ns per point | One heap allocation per QVariant plus the append |
| Simulator delivery at the nominal workload | >= 99% of requested frames, inter-arrival jitter < 5 ms | Frames come from a pre-built pool, so emission is a pointer copy |
| Idle scene frame interval | Equals the display refresh period, p99 within 1 ms of the median | A trivial scene is vsync-bound |
| Event-loop lag in the idle scene | p99 < 1 ms | Nothing is running on the GUI thread |
