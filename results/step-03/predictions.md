# Step 3: predictions (written before the code exists)

## The feature

Four camera panels (1080p, 30 fps, H.264) under the 3D or top-down view, each labelled with its frame rate and its time offset from the lidar. In the lab panel:
- **Video path:** *forward* the decoded frame to a `VideoOutput` unchanged, or *convert to image* (`QVideoFrame::toImage()`), which is what code that wants to draw on or inspect frames often does.
- **Sync:** show the *latest* camera frame, or the camera frame *aligned* to the newest lidar time (buffered; the lidar is the master clock).
- **Decoder:** software, or VA-API hardware (`--set hwdecode=0|1`; needs a restart).

Test videos are generated with ffmpeg (`testsrc2`, camera id and frame number burned in, H.264 High profile).

## Copy budget, before measuring

One 1080p NV12 frame is 3.1 MB; 4 cameras × 30 fps = 120 frames/s, so each full copy of every frame is **373 MB/s** (RGBA: 8.3 MB, 995 MB/s).

| Path | Copies per frame (guess) |
|---|---|
| Software decode, forward | Decoder writes NV12 to CPU memory; 1 upload to the GPU; YUV→RGB on the GPU |
| VA-API decode, forward | Qt's docs say hardware texture conversion is off by default with VA-API, so: GPU → CPU download, then upload again. **2 copies; hardware decode is not zero-copy here** |
| Either decoder, convert to image | Plus a CPU YUV→RGBA conversion (read 3.1 MB, write 8.3 MB) and an 8.3 MB RGBA upload |

## Guesses

| Question | Guess |
|---|---|
| Frame handle type (`QVideoFrame::handleType`) | `NoHandle` (CPU memory) for both decoders |
| Thread `videoFrameChanged` arrives on | The GUI thread |
| Process CPU added by 4 cameras, software decode, forward | +60–120% |
| Same with VA-API | +15–40% |
| Convert-to-image path | 3–6 ms per frame **on the GUI thread** → 120 frames/s × 4 ms ≈ 0.5 s/s: GUI lag and missed frames |
| Camera latency (capture → photon), forward | 30–50 ms |
| Camera latency, convert to image | 50–100 ms |
| Camera–lidar skew, *latest* policy | p50 within ±20 ms, p99 ≈ 50 ms |
| Camera–lidar skew, *aligned* policy | \|skew\| p99 ≤ 20 ms, at the cost of 15–35 ms more camera latency |
| Media clock vs our clock over 60 s | Drift < 5 ms (both come from the same monotonic clock) |
| Memory | +100–200 MB for four decoders |
| The lidar view while cameras play (forward path) | Unaffected: 60 fps, GUI lag p99 within 1 ms of no cameras |
