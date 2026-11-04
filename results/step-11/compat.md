# Step 11: old and new viewers over the wire

Server at nominal load (4 × 150k @ 10 Hz, 200 objects, 200 events/s), cameras off; the client benchmarks 10 s after 3 s over TCP (`scripts/step11_compat.py`; old = `step-10` built from the tag).

| Pair | Lidar frames/s shown | Latency p50 | Seconds with a probed clock | Server's line for the client | stderr |
|---|---|---|---|---|---|
| old client (step-10), new server | 40.1 | 30.3 ms | 0 | ::ffff:127.0.0.1:54222 · 21.9 MB/s · backlog 0.5 MB · 0 replaced · 0 events dropped | nothing |
| new client, old server (step-10) | 40.1 | 30.1 ms | 0 | ::ffff:127.0.0.1:60666 · 21.9 MB/s · backlog 0.0 MB · 0 replaced · 0 events dropped | nothing |
| new client, new server | 40.0 | 31.3 ms | 10 | ::ffff:127.0.0.1:55122 (credit) · 21.9 MB/s · backlog 0.6 MB · 0 replaced · 0 events dropped | nothing |

The browser has no TCP: a page needs a server with the WebSocket endpoint (step 11 or later).
