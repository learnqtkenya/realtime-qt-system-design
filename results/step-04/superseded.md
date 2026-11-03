# Superseded measurements (before the fixes)

The notes quote "before" numbers that later runs replaced. Their raw reports were overwritten by the re-runs and not kept. The values below are copied from `scripts/step04_report.py` output at the time, with the build each came from.

## First sweep (commit 0dc8897: one chunk stream, a camera seek per mouse move, 16-frame replay ring)

| Run (median of 2) | Seek p50 / p99 | Cam seek p50 / p99 | Cam latency p50 | \|skew\| p50 | GUI lag p99 | CPU % |
|---|---|---|---|---|---|---|
| scrub-memory | 0.1 / 0.2 ms | 110.9 / 160.7 ms | 136.2 ms | 2,937 ms | **4.5 ms** | **137.9** |
| seek-file-warm (mixed chunks) | **199.1** / 226.8 ms | 86.3 / 144.0 ms | 30.2 ms | – | 0.8 ms | 67.7 |
| seek-file-cold (mixed chunks) | **577.4** / 642.6 ms | 64.7 / 143.0 ms | 68.2 ms | – | 0.8 ms | 114.4 |
| replay-file-4x | – | – | **no camera frames shown** | – | 3.0 ms | **140.4** |

The mixed-chunk seeks were re-measured on the final build (`seek-file-*-mixed` in `raw/`: 196 / 577 ms), so that comparison stands on current files.

## Before the camera replay buffer (exploratory run, same day)

Replay of a 12 s recording at 1×, four cameras: camera latency p50 91.6 ms, |skew| p50 69.7 ms, p99 87.1 ms. After the 150 ms lead and due-time buffer: 22.0 ms, 9.2 ms, 29.6 ms (final sweep: 23.4 ms and 10.9 ms).

## Before the encoder fixes

- Writer, encode + append per lidar frame: 0.97 + 1.14 ms (copy into a chunk buffer that was freed every chunk). After encoding in place: 1.0 ms total. After restoring vectorisation: 0.55 ms.
- History int16 encode: 1.39 ms per frame (`history_encode_ms`, 20 s run). After restoring vectorisation: 0.6 ms (sweep `history-fresh-int16-1000`).
