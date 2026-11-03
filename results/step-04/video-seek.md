# Paused seek to the exact frame: keyframe interval and parallel decoders

30 s 1080p30 H.264 (High, no B-frames), Qt Multimedia FFmpeg backend (VA-API), 100 random seeks per decoder, 50 ms between seeks. Latency: `setPosition()` to the frame arriving in the sink. Raw rows in `video-seek/*.csv` (stderr in `*.err`).

| Keyframe every | File | Decoders | Seeks | p50 ms | p99 ms | max ms | Exact frame | Median by time since keyframe (0–250 / 250–500 / 500–750 / 750–1000 ms) |
|---|---|---|---|---|---|---|---|---|
| 30 frames (1.00 s) | 24.3 MB | 1 | 100 | 21.2 | 48.3 | 48.3 | yes | 13 / 19 / 25 / 32 |
| 30 frames (1.00 s) | 24.3 MB | 4 | 400 | 53.6 | 104.9 | 112.6 | yes | 33 / 50 / 67 / 85 |
| 10 frames (0.33 s) | 26.2 MB | 1 | 100 | 14.2 | 38.2 | 38.2 | yes | 13 / 18 / – / – |
| 10 frames (0.33 s) | 26.2 MB | 4 | 400 | 27.4 | 43.5 | 53.3 | yes | 26 / 34 / – / – |
| 1 frames (0.03 s) | 46.5 MB | 1 | 100 | 13.3 | 33.1 | 33.1 | yes | 13 / – / – / – |
| 1 frames (0.03 s) | 46.5 MB | 4 | 400 | 28.7 | 41.0 | 61.4 | yes | 29 / – / – / – |
