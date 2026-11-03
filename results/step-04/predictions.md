# Step 4: predictions (written before the code exists)

## The feature

A **timeline bar** under the view:
- **Pause, play, step** one lidar frame, **scrub** by dragging, replay at 0.25×–4×, and **go live**.
- **Record** to disk: a red button shows the elapsed time, the size and the write rate. A recording can be opened and replayed with the same controls.
- **Presentation delay** (lab panel): every stream is shown at *now − D*, so the streams agree on "now". This is the shared clock that step 3 asked for.
- **Simulated lidar processing delay** (lab panel), so the streams' pipelines differ the way they do on a real vehicle (for example 150 ms of lidar processing against 25 ms for camera decoding).

Cameras follow the timeline by **seeking their source video**, not by storing frames. A paused seek delivers the exact frame in 20–36 ms (tested before designing, `QMediaPlayer` FFmpeg backend).

## Capacity, before measuring

| Stream at nominal load | Rate |
|---|---|
| Lidar, float32 xyz (4 × 150k × 12 B × 10 Hz) | **72 MB/s** = 4.3 GB/min |
| Lidar, int16 at 1 cm (6 B per point) | 36 MB/s |
| Tracks (50 × ~28 B × 10 Hz) | 14 KB/s |
| Events (50/s; 2,000/s peak, ~40 B each) | 2 KB/s; 80 KB/s |
| Cameras decoded (4 × 1080p30 NV12) | 373 MB/s |
| Cameras as encoded H.264 (the source files) | **3.3 MB/s** |
| Camera index (time ↔ media position, 120/s) | ~3 KB/s |

So the lidar is the storage problem: 99.99% of the bytes are lidar. Keeping decoded camera frames would be 5× worse, so cameras are kept as the encoded stream plus an index.

Machine: 29 GB RAM, NVMe with 133 GB free. A one-minute recording is 2–4 GB; a 30-minute shift would not fit as float32.

## Design guesses

- **History in memory:** the frames are already immutable and shared (step 1), so keeping one is keeping a pointer. Budgeted in bytes, not seconds: 1 GB ≈ 14 s of float32 or ≈ 28 s of int16.
- **A trap I expect:** the simulator reuses four clouds per sensor, so a history of shared pointers would cost no memory and look free. A real sensor allocates every frame. Before measuring memory, the simulator must allocate every frame too.
- **On disk:** an append-only file of chunks (~4 MB), each with a CRC and a time range, plus an index at the end. Readers keep the index in memory and seek with a binary search. If the app dies mid-recording, the missing index is rebuilt by scanning the chunks.
- **Ordering:** records are written in arrival order, but replay must be in capture order. Arrival and capture order differ when pipelines have different delays, so chunks overlap in time; the index, not the file order, gives time order.
- **Schema evolution:** every record carries a type and a version. Readers skip unknown types and ignore trailing fields they don't know.

## Guesses

| Question | Guess |
|---|---|
| Simulator CPU to allocate every frame | +1–3% of one core (72 MB/s of memcpy) |
| Camera–lidar \|skew\| p50, *latest*, with 150 ms of lidar processing | ≈ 150 ms (the camera is newer than the lidar by the pipeline difference) |
| Same with a shared clock, D = 200 ms | ≤ 15 ms p50, ≤ 35 ms p99, back to step 3's level |
| Lidar latency with D = 200 ms | ≈ 200 + render ≈ 225 ms: sync is bought with latency |
| History memory, 1 GB budget | RSS rises ~1 GB, then flat |
| History ingest cost (per lidar frame, sim thread) | Shared pointer: < 0.01 ms. int16 quantising 150k points: 0.2–0.5 ms |
| Seek in memory (index lookup + hand frames to renderers) | < 1 ms, plus the normal 25 ms render path |
| Scrubbing (drag), input-to-photon p99 | ≤ 50 ms, within the service level |
| Recording write rate, float32, uncompressed | 72 MB/s, well within NVMe; writer queue stays short |
| zstd level 1 on float32 points | Ratio 1.1–1.3 (noisy mantissas compress badly), ~300–500 MB/s per core |
| zstd level 1 on int16 points | Ratio 1.5–2 |
| Seek in a file, warm page cache | 2–5 ms (read and decompress one chunk) |
| Seek in a file, cold (dropped from page cache) | 10–30 ms |
| Replay at 1× of a float32 recording | Pixel-identical to live (parity test) |
| Replay of an int16 recording | Not identical; max error 5 mm, < 1 px at the default zoom |
| Index size | < 0.1% of the file |
| Recovering a file cut mid-chunk | Every complete chunk recovered; recovery reads the whole file once |
| Camera seek on replay (paused) | 20–40 ms to the exact frame; keyframes every 1 s don't make it slower |
