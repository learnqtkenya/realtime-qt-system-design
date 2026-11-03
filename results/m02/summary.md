# m02 sweep summary

Environment: {"governor": "powersave", "session": "wayland"}

Stage columns are milliseconds per second of wall time on the thread that runs the stage (1000 = that thread did nothing else).

## Series control

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | exit | ext pause | stderr | gpu p50 | gpu p99 | proc cpu% | canary ms | paint_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| control-nosim | 10,000 | 50 | 50 | 59.9 | 16.7 | 18.4 | 22.9 | 5 | 0.8 | 33.8 | 1.8 | - | - | - | 0.0 | no | 0 | - | 0 | - | - | 6.5 | - | 0.0 | 0.0 | 0.0 |

## Series A

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | exit | ext pause | stderr | gpu p50 | gpu p99 | proc cpu% | canary ms | paint_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A-pts1k | 1,000 | 50 | 50 | 59.9 | 16.7 | 18.1 | 21.3 | 2 | 5.2 | 14.0 | 12.1 | 25.3 | 34.1 | 0 | 1.3 | no | 0 | - | 0 | - | - | 22.1 | 2.6 | 16.9 | 28.2 | 65.6 |
| A-pts5k | 5,000 | 50 | 50 | 59.9 | 16.7 | 18.0 | 20.9 | 2 | 6.3 | 13.7 | 12.6 | 25.1 | 33.6 | 0 | 1.5 | no | 0 | - | 0 | - | - | 27.7 | 2.7 | 69.6 | 27.8 | 69.0 |
| A-pts10k | 10,000 | 50 | 50 | 59.9 | 16.7 | 17.9 | 21.2 | 1 | 7.2 | 13.4 | 11.8 | 25.1 | 34.2 | 0 | 1.1 | no | 0 | - | 0 | - | - | 31.9 | 2.5 | 120.8 | 26.5 | 64.8 |
| A-pts25k | 25,000 | 50 | 50 | 59.9 | 16.7 | 18.0 | 22.3 | 3 | 34.6 | 63.4 | 11.3 | 28.1 | 49.6 | 1 | 1.0 | no | 0 | - | 0 | - | - | 45.5 | 2.6 | 263.6 | 27.1 | 61.4 |
| A-pts50k | 50,000 | 50 | 50 | 54.5 | 16.8 | 47.6 | 58.7 | 156 | 73.9 | 216.0 | 13.8 | 29.6 | 156.1 | 5 | 13.9 | no | 0 | - | 0 | - | - | 74.8 | 2.8 | 543.5 | 33.4 | 83.0 |
| A-pts100k | 100,000 | 50 | 50 | 24.6 | 45.4 | 72.2 | 85.3 | 296 | 255.3 | 410.1 | 17.6 | 102.0 | 261.2 | 9 | 16.8 | no | 0 | - | 0 | - | - | 98.8 | 4.1 | 761.3 | 42.2 | 109.7 |
| A-pts150k | 150,000 | 50 | 50 | 17.7 | 62.3 | 85.5 | 99.0 | 226 | 284.5 | 296.4 | 15.0 | 127.5 | 210.0 | 7 | 3.6 | no | 0 | - | 0 | - | - | 100.8 | 3.4 | 823.1 | 36.0 | 92.0 |

## Series B

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | exit | ext pause | stderr | gpu p50 | gpu p99 | proc cpu% | canary ms | paint_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B-obj50 | 1,000 | 50 | 50 | 59.9 | 16.7 | 18.0 | 20.2 | 1 | 8.3 | 15.6 | 16.2 | 25.3 | 33.6 | 0 | 1.3 | no | 0 | - | 0 | - | - | 28.2 | 3.1 | 19.7 | 33.9 | 89.9 |
| B-obj200 | 1,000 | 200 | 50 | 60.0 | 16.7 | 22.0 | 32.3 | 24 | 18.9 | 42.0 | 24.3 | 25.3 | 62.0 | 2 | 0.9 | no | 0 | - | 0 | - | - | 38.2 | 10.2 | 20.7 | 109.9 | 81.6 |
| B-obj400 | 1,000 | 400 | 50 | 58.7 | 16.7 | 36.4 | 58.4 | 84 | 25.9 | 62.0 | 30.1 | 25.3 | 61.2 | 1 | 1.1 | no | 0 | - | 0 | - | - | 44.2 | 15.4 | 15.4 | 170.6 | 67.6 |
| B-obj800 | 1,000 | 800 | 50 | 51.7 | 16.6 | 47.6 | 63.1 | 151 | 44.8 | 74.8 | 45.0 | 29.5 | 74.6 | 2 | 0.3 | no | 0 | - | 0 | - | - | 61.0 | 28.1 | 11.8 | 287.6 | 66.3 |

## Series C

| label | pts/frame | objects | ev/s | fps | frame p50 | frame p99 | frame max | >20ms | lag p99 | lag max | gui cpu% | lat p50 | lat max | backlog max | rss MB/min | stalled | exit | ext pause | stderr | gpu p50 | gpu p99 | proc cpu% | canary ms | paint_ms ms/s | tracks_rebuild_ms ms/s | event_reset_ms ms/s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| C-ev50 | 1,000 | 50 | 50 | 59.9 | 16.7 | 17.8 | 18.2 | 0 | 4.8 | 8.8 | 11.4 | 25.2 | 34.3 | 0 | 1.2 | no | 0 | no | 0 | - | - | 21.4 | 2.5 | 17.5 | 25.1 | 62.5 |
| C-ev500 | 1,000 | 50 | 500 | 59.9 | 16.7 | 17.8 | 19.2 | 0 | 4.6 | 11.0 | 11.8 | 25.0 | 34.3 | 0 | 6.8 | no | 0 | no | 0 | - | - | 21.8 | 2.5 | 17.8 | 26.3 | 64.3 |
| C-ev2000 | 1,000 | 50 | 2,000 | 59.9 | 16.7 | 17.9 | 20.5 | 2 | 4.3 | 9.4 | 11.4 | 25.1 | 35.3 | 0 | 21.3 | no | 0 | no | 0 | - | - | 21.3 | 2.5 | 16.7 | 25.6 | 62.5 |
