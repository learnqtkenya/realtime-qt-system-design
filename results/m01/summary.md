# m01 sweep summary

Environment: {"governor": "powersave", "session": "wayland"}

Stage columns are milliseconds per second of wall time on the thread that runs the stage (1000 = that thread did nothing else).

## Series control

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | exit | ext pause | stderr | gpu p50 | gpu p99 | proc cpu% | canary ms | convert_points_ms ms/s | lidar_signal_ms ms/s | canvas_paint_ms ms/s | convert_tracks_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| control-nosim | 10,000 | 50 | 50 | 59.9 | 16.7 | 17.9 | 18.3 | 0 | 0.5 | 3.4 | 1.6 | - | - | - | 0.1 | no | 0 | - | 0 | - | - | 6.3 | - | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |

## Series A

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | exit | ext pause | stderr | gpu p50 | gpu p99 | proc cpu% | canary ms | convert_points_ms ms/s | lidar_signal_ms ms/s | canvas_paint_ms ms/s | convert_tracks_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A-pts1k | 1,000 | 50 | 50 | 60.0 | 16.7 | 17.5 | 17.9 | 0 | 7.4 | 14.3 | 22.2 | 25.1 | 34.5 | 0 | 0.9 | no | 0 | - | 0 | - | - | 31.5 | 2.9 | 1.9 | 12.8 | 38.1 | 1.0 | 29.9 | 64.8 |
| A-pts5k | 5,000 | 50 | 50 | 59.9 | 16.7 | 17.8 | 20.1 | 1 | 13.5 | 22.2 | 44.1 | 25.2 | 34.5 | 0 | 2.5 | no | 0 | - | 0 | - | - | 53.4 | 2.9 | 7.0 | 54.9 | 114.3 | 0.8 | 29.5 | 59.2 |
| A-pts10k | 10,000 | 50 | 50 | 59.9 | 16.7 | 20.5 | 36.7 | 17 | 36.0 | 46.9 | 67.2 | 29.0 | 49.9 | 0 | -2.0 | no | 0 | - | 0 | - | - | 76.7 | 2.7 | 12.6 | 91.9 | 182.8 | 0.7 | 27.9 | 56.8 |
| A-pts25k | 25,000 | 50 | 50 | 32.9 | 33.7 | 56.3 | 72.1 | 350 | 175.4 | 232.6 | 96.0 | 63.3 | 146.7 | 4 | 50.6 | no | 0 | - | 0 | - | - | 102.0 | 2.3 | 25.1 | 202.9 | 241.1 | 0.6 | 24.2 | 50.2 |
| A-pts50k | 50,000 | 50 | 50 | 8.7 | 115.6 | 305.0 | 323.2 | 110 | 1,640.3 | 1,643.3 | 98.7 | 598.2 | 1,136.8 | 44 | 3,219.6 | no | 0 | - | 0 | - | - | 101.0 | 2.1 | 81.1 | 386.2 | 149.4 | 0.5 | 26.3 | 59.1 |
| A-pts100k | 100,000 | 50 | 50 | 0.9 | 545.9 | 3,274.9 | 3,402.4 | 8 | 4,464.6 | 4,485.5 | 99.8 | 2,027.9 | 3,542.3 | 140 | 16,421.9 | yes | 0 | - | 0 | - | - | 100.2 | 2.1 | 84.2 | 709.3 | 22.2 | 0.3 | 18.4 | 102.1 |
| A-pts150k | 150,000 | 50 | 50 | 0.7 | 1,076.0 | 4,350.9 | 4,526.2 | 9 | 9,406.6 | 9,406.6 | 99.9 | 4,360.4 | 7,979.0 | 356 | 17,153.2 | yes | 4 | - | 0 | - | - | 100.4 | 2.1 | 82.0 | 719.3 | 39.4 | 0.2 | 41.5 | 52.1 |

## Series B

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | exit | ext pause | stderr | gpu p50 | gpu p99 | proc cpu% | canary ms | convert_points_ms ms/s | lidar_signal_ms ms/s | canvas_paint_ms ms/s | convert_tracks_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B-obj50 | 1,000 | 50 | 50 | 59.9 | 16.7 | 18.0 | 19.2 | 0 | 7.8 | 15.2 | 22.5 | 25.2 | 34.3 | 0 | 12.5 | no | 0 | - | 0 | - | - | 32.2 | 2.9 | 1.8 | 12.8 | 38.6 | 1.0 | 30.5 | 65.8 |
| B-obj200 | 1,000 | 200 | 50 | 59.9 | 16.7 | 18.0 | 27.5 | 1 | 17.4 | 39.4 | 29.9 | 25.2 | 40.0 | 1 | 2.6 | no | 0 | - | 0 | - | - | 40.6 | 9.5 | 1.9 | 11.7 | 40.1 | 3.0 | 96.2 | 62.7 |
| B-obj400 | 1,000 | 400 | 50 | 60.0 | 16.6 | 28.1 | 30.7 | 106 | 29.5 | 38.3 | 39.3 | 26.2 | 40.0 | 0 | 0.3 | no | 0 | - | 0 | - | - | 51.9 | 17.3 | 1.7 | 10.4 | 39.7 | 6.1 | 174.1 | 59.1 |
| B-obj800 | 1,000 | 800 | 50 | 47.8 | 16.4 | 53.7 | 77.0 | 162 | 56.4 | 83.9 | 58.2 | 27.4 | 83.4 | 2 | 0.6 | no | 0 | - | 0 | - | - | 73.0 | 34.4 | 2.2 | 10.5 | 33.8 | 11.0 | 350.5 | 68.7 |

## Series C

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | exit | ext pause | stderr | gpu p50 | gpu p99 | proc cpu% | canary ms | convert_points_ms ms/s | lidar_signal_ms ms/s | canvas_paint_ms ms/s | convert_tracks_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| C-ev50 | 1,000 | 50 | 50 | 60.0 | 16.7 | 18.0 | 18.6 | 0 | 6.3 | 12.9 | 21.3 | 25.2 | 34.0 | 0 | 0.8 | no | 0 | - | 0 | - | - | 31.0 | 2.8 | 1.8 | 12.7 | 35.5 | 1.0 | 28.5 | 63.4 |
| C-ev500 | 1,000 | 50 | 500 | 60.0 | 16.7 | 17.8 | 18.1 | 0 | 6.9 | 16.9 | 21.8 | 25.1 | 34.1 | 0 | 10.2 | no | 0 | - | 0 | - | - | 31.7 | 2.9 | 1.8 | 12.7 | 35.8 | 1.0 | 29.6 | 64.5 |
| C-ev2000 | 1,000 | 50 | 2,000 | 59.9 | 16.7 | 17.9 | 18.5 | 0 | 6.5 | 50.1 | 22.3 | 25.2 | 36.6 | 1 | 20.0 | no | 0 | - | 0 | - | - | 31.8 | 2.9 | 1.8 | 13.0 | 38.1 | 1.0 | 30.0 | 65.9 |
