# Step 10 analysis

Medians of repeats; 20 s each after 3 s. *CPU %* is of one core.

## A. Eight lidars (nominal otherwise, cameras off)

| Run | n | Frame p99 | GUI lag p99 | Frames > 20 ms | Latency p50 | Latency p99 | Lidar frames/s shown | Camera latency p50 | CPU % | GPU ms | RSS MB |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 4 × 150k @ 10 Hz | 2 | 17.7 | 2.82 | 0 | 25.4 | 33.7 | 40 of 40 | – | 26 | 1.54 | 1,175 |
| 8 × 150k @ 10 Hz | 2 | 17.6 | 3.00 | 1 | 22.2 | 29.4 | 80 of 80 | – | 29 | 2.86 | 1,205 |
| 4 × 150k @ 10 Hz, recording | 1 | 17.8 | 2.69 | 0 | 24.5 | 33.0 | 40 of 40 | – | 38 | 1.54 | 1,183 |
| 8 × 150k @ 10 Hz, recording | 1 | 17.6 | 2.82 | 0 | 22.2 | 29.9 | 80 of 80 | – | 52 | 2.86 | 1,213 |

Recording (int16 planar + zstd 1): files of 504 MB (4 lidars) and 1,004 MB (8 lidars) for the same 23 s, i.e. 21.9 and 43.6 MB/s.

| Lidars | Frames encoded/s | Encode ms p50 | Chunk write ms p50 |
|---|---|---|---|
| 4 | 40 | 0.71 | 1.11 |
| 8 | 80 | 0.73 | 1.09 |

Second viewer over the wire (as built):

| Lidars on the server | Received MB/s | Lidar frames/s shown by the client | Client latency p50 / p99 |
|---|---|---|---|
| 4 | 21.9 | 40 | 29.3 / 39.0 |
| 8 | 43.8 | 40 | 26.8 / 38.3 |

Second viewer over the wire (client sized for eight sensors):

| Lidars on the server | Received MB/s | Lidar frames/s shown by the client | Client latency p50 / p99 |
|---|---|---|---|
| 4 | 21.9 | 40 | 30.1 / 40.0 |
| 8 | 43.8 | 80 | 26.8 / 34.6 |

## C. A smaller machine: cores (`taskset`)

Nominal: 4 × 150k @ 10 Hz, 200 objects, 200 events/s. Peak: 4 × 300k @ 20 Hz, 800 objects, 2,000 events/s. Cameras: four 1080p30 (VA-API).

| Run | n | Frame p99 | GUI lag p99 | Frames > 20 ms | Latency p50 | Latency p99 | Lidar frames/s shown | Camera latency p50 | CPU % | GPU ms | RSS MB |
|---|---|---|---|---|---|---|---|---|---|---|---|
| nominal, 16 cores | 2 | 17.8 | 2.76 | 0 | 25.0 | 33.4 | 40 of 40 | – | 25 | 1.54 | 1,174 |
| nominal, 2 cores | 2 | 18.2 | 2.63 | 1 | 25.2 | 33.8 | 40 of 40 | – | 22 | 1.54 | 1,177 |
| nominal + cameras, 16 cores | 2 | 17.9 | 2.98 | 0 | 25.0 | 33.5 | 40 of 40 | 25.8 | 35 | 1.85 | 1,426 |
| nominal + cameras, 2 cores | 2 | 18.2 | 2.92 | 2 | 25.1 | 33.7 | 40 of 40 | 25.2 | 28 | 1.83 | 1,241 |
| nominal + cameras, 1 core | 2 | 18.0 | 3.20 | 0 | 24.9 | 34.1 | 40 of 40 | 23.9 | 25 | 1.79 | 1,221 |
| peak + cameras, 16 cores | 2 | 17.8 | 2.10 | 1 | 22.3 | 30.0 | 80 of 80 | 25.4 | 48 | 3.20 | 1,462 |
| peak + cameras, 2 cores | 2 | 18.4 | 2.47 | 0 | 22.3 | 29.8 | 80 of 80 | 25.3 | 37 | 3.22 | 1,278 |
| peak + cameras, 1 core | 2 | 18.4 | 4.04 | 1 | 23.0 | 30.0 | 80 of 80 | 25.3 | 38 | 3.22 | 1,258 |

## C. A smaller machine: memory (a 1 GB cgroup limit, no swap)

| Run | Cameras | Outcome | RSS MB (last) | History MB | History s |
|---|---|---|---|---|---|
| mem-1g-asbuilt | no cameras | **killed** after 12 s | 974 | 797 | 10.9 |
| mem-1g-adaptive | no cameras | ran the full 148 s | 532 | 321 | 4.3 |
| mem-1g-adaptive-cameras | cameras | ran the full 148 s | 838 | 321 | 4.4 |

## B. Notes across versions

See [`compat.md`](compat.md).
