# Step 11: the WebSocket endpoint, measured natively

A serving viewer at nominal load (4 × 150k @ 10 Hz, 200 objects, 200 events/s, cameras off, zstd 1), with a second
viewer connected to it on loopback. Three alternating runs per transport, 20 s each after 3 s (`scripts/step11_wire.py`).

| Transport | Latency p50 (runs) | Latency p99 (runs) | Lidar frames/s shown | Received MB/s |
|---|---|---|---|---|
| TCP | 30.5, 29.4, 29.7 | 39.7, 38.9, 39.1 | 39.9–40.0 of 40 | 21.9–22.0 |
| WebSocket | 28.7, 29.8, 29.2 | 38.3, 39.2, 38.7 | 39.9–40.0 of 40 | 21.9–22.1 |

The two are within run-to-run noise: WebSocket framing is a few bytes per record. Nothing on stderr; the server exited 0.
