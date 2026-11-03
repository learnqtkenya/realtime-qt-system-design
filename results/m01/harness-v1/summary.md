# Module 1 sweep summary

Environment: {"governor": "powersave", "session": "wayland"}

Stage columns are GUI-thread milliseconds per second of wall time (1000 = the thread did nothing else).

## Series control

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | convert_points_ms ms/s | lidar_signal_ms ms/s | canvas_paint_ms ms/s | convert_tracks_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| control-nosim | 10,000 | 50 | 50 | 1.0 | 1,000.0 | 1,000.7 | 1,000.7 | 15 | 0.2 | 3.0 | 1.1 | - | - | - | 0.1 | no | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |

## Series A

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | convert_points_ms ms/s | lidar_signal_ms ms/s | canvas_paint_ms ms/s | convert_tracks_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A-pts1k | 1,000 | 50 | 50 | 59.4 | 16.8 | 28.7 | 33.6 | 202 | 7.7 | 15.2 | 21.9 | 13.1 | 33.2 | 0 | 13.5 | no | 2.1 | 12.4 | 33.4 | 0.9 | 29.0 | 66.3 |
| A-pts5k | 5,000 | 50 | 50 | 56.5 | 17.1 | 29.1 | 35.0 | 283 | 14.4 | 55.0 | 43.0 | 17.5 | 58.7 | 1 | 16.0 | no | 7.5 | 54.1 | 105.5 | 0.8 | 28.9 | 58.0 |
| A-pts10k | 10,000 | 50 | 50 | 53.7 | 16.9 | 31.8 | 53.6 | 276 | 20.8 | 42.8 | 66.0 | 22.5 | 51.0 | 1 | 13.1 | no | 12.4 | 89.8 | 184.2 | 0.6 | 25.9 | 52.2 |
| A-pts25k | 25,000 | 50 | 50 | 25.8 | 40.3 | 59.5 | 67.3 | 351 | 119.1 | 138.8 | 95.1 | 51.7 | 105.4 | 3 | -74.2 | no | 24.5 | 195.5 | 239.5 | 0.5 | 23.8 | 50.1 |
| A-pts50k | 50,000 | 50 | 50 | 8.1 | 125.2 | 231.7 | 248.4 | 116 | 687.5 | 778.6 | 99.0 | 227.7 | 449.7 | 16 | 102.3 | no | 46.6 | 387.4 | 154.1 | 0.7 | 23.0 | 48.5 |
| A-pts100k | 100,000 | 50 | 50 | 1.4 | 614.4 | 2,304.1 | 2,479.6 | 21 | 3,872.7 | 3,896.5 | 99.8 | 2,403.1 | 4,831.1 | 192 | 6,334.3 | yes | 75.8 | 647.2 | 55.4 | 0.2 | 57.9 | 63.9 |
| A-pts150k | 150,000 | 50 | 50 | 0.8 | 780.7 | 4,454.3 | 4,668.9 | 12 | 7,721.4 | 7,721.4 | 99.6 | 4,440.2 | 8,399.8 | 358 | 0 | yes | 79.6 | 691.1 | 54.5 | 0.1 | 40.4 | 50.8 |

## Series B

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | convert_points_ms ms/s | lidar_signal_ms ms/s | canvas_paint_ms ms/s | convert_tracks_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B-obj50 | 1,000 | 50 | 50 | 58.8 | 16.9 | 30.1 | 32.1 | 161 | 7.0 | 13.5 | 21.3 | 13.8 | 33.3 | 0 | 1.5 | no | 1.9 | 12.1 | 32.1 | 1.0 | 28.9 | 63.7 |
| B-obj200 | 1,000 | 200 | 50 | 56.7 | 16.7 | 40.7 | 44.5 | 210 | 16.6 | 30.6 | 29.1 | 14.2 | 33.4 | 0 | 1.7 | no | 2.1 | 11.0 | 34.4 | 3.2 | 94.9 | 61.0 |
| B-obj400 | 1,000 | 400 | 50 | 55.3 | 16.6 | 47.7 | 62.0 | 187 | 30.1 | 41.3 | 39.8 | 19.5 | 56.7 | 1 | 0.2 | no | 1.6 | 9.1 | 39.6 | 5.6 | 178.1 | 59.8 |
| B-obj800 | 1,000 | 800 | 50 | 38.5 | 16.2 | 77.4 | 83.9 | 163 | 55.7 | 74.3 | 57.8 | 18.9 | 70.8 | 2 | 0.7 | no | 2.4 | 10.6 | 30.2 | 9.8 | 350.7 | 69.9 |

## Series C

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | convert_points_ms ms/s | lidar_signal_ms ms/s | canvas_paint_ms ms/s | convert_tracks_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| C-ev50 | 1,000 | 50 | 50 | 59.0 | 16.8 | 28.7 | 33.7 | 414 | 7.4 | 12.6 | 22.4 | 13.4 | 33.8 | 0 | 8.0 | no | 2.0 | 12.5 | 35.4 | 1.0 | 30.4 | 65.8 |
| C-ev500 | 1,000 | 50 | 500 | 59.1 | 16.8 | 29.2 | 32.4 | 390 | 6.8 | 13.8 | 22.0 | 14.0 | 34.1 | 0 | 9.1 | no | 1.9 | 12.5 | 36.0 | 0.9 | 29.4 | 64.5 |
| C-ev2000 | 1,000 | 50 | 2,000 | 59.3 | 16.7 | 28.7 | 33.2 | 421 | 6.5 | 14.5 | 22.2 | 13.8 | 34.1 | 0 | 21.6 | no | 1.9 | 12.6 | 34.9 | 1.0 | 30.4 | 65.5 |
