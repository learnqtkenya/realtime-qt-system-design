# Step 8 analysis

Medians of the repeats (n). Nominal lidar (4 × 150k points at 10 Hz), 200 events/s, cameras off, RHI renderer. The layer is speed vectors in every case: one line per object.

Columns: GUI lag and frame time p99 in ms; GUI-thread and whole-process CPU in % of one core; RSS max in MB; *work per update* is the layer's own cost on its thread (scripts: input conversion + `update()`, ms p50; processes: the plugin process's CPU); *latency* is tracks presented → overlay on the GUI thread (ms); *sync* is building the overlay nodes on the render thread (ms p50).

## Cost, 200 objects

| Layer | n | GUI lag p99 | Frame p99 | GUI CPU | Process CPU | RSS | Work per update | Latency p50 | Latency p99 | Sync |
|---|---|---|---|---|---|---|---|---|---|---|
| no layer | 2 | 2.72 | 17.8 | 6.2 | 26.3 | 1,173 | – | – | – | – |
| item (viewer's engine) | 2 | 2.85 | 17.9 | 6.9 | 27.1 | 1,174 | – | – | – | – |
| script, JS module, GUI thread | 2 | 3.22 | 17.8 | 7.8 | 28.0 | 1,177 | 0.37 + 0.10 | 0.49 | 0.91 | 0.006 |
| script, QML object, GUI thread | 2 | 3.24 | 17.8 | 7.7 | 27.7 | 1,178 | 0.36 + 0.10 | 0.50 | 0.98 | 0.006 |
| script, JS module, own thread | 2 | 3.18 | 17.8 | 7.7 | 29.2 | 1,176 | 0.52 + 0.17 | 0.85 | 4.07 | 0.007 |
| script, QML object, own thread | 2 | 3.20 | 17.8 | 7.3 | 28.3 | 1,177 | 0.52 + 0.14 | 0.81 | 4.39 | 0.008 |
| process, Python | 2 | 2.86 | 17.8 | 6.5 | 27.0 | 1,177 | plugin CPU 0.5% | 0.78 | 4.49 | 0.008 |
| process, Python, in bwrap | 2 | 3.03 | 17.7 | 6.7 | 27.2 | 1,177 | plugin CPU 0.6% | 0.84 | 5.71 | 0.007 |
| process, C++ | 2 | 2.81 | 17.6 | 6.3 | 26.5 | 1,177 | plugin CPU 0.0% | 0.31 | 3.96 | 0.007 |

## Cost, 2000 objects

| Layer | n | GUI lag p99 | Frame p99 | GUI CPU | Process CPU | RSS | Work per update | Latency p50 | Latency p99 | Sync |
|---|---|---|---|---|---|---|---|---|---|---|
| no layer | 2 | 1.43 | 17.7 | 6.4 | 23.6 | 1,174 | – | – | – | – |
| item (viewer's engine) | 2 | 8.74 | 17.8 | 13.6 | 31.4 | 1,187 | – | – | – | – |
| script, JS module, GUI thread | 2 | 4.24 | 17.8 | 11.0 | 28.8 | 1,179 | 2.01 + 0.78 | 2.81 | 3.94 | 0.018 |
| script, QML object, GUI thread | 2 | 4.61 | 17.7 | 11.3 | 28.8 | 1,179 | 2.19 + 0.80 | 3.00 | 3.95 | 0.021 |
| script, JS module, own thread | 2 | 1.92 | 17.7 | 8.0 | 29.1 | 1,179 | 2.28 + 0.78 | 3.18 | 4.86 | 0.022 |
| script, QML object, own thread | 2 | 2.11 | 17.8 | 7.7 | 28.9 | 1,180 | 2.68 + 0.79 | 3.66 | 7.87 | 0.020 |
| process, Python | 2 | 1.61 | 17.8 | 7.1 | 24.9 | 1,179 | plugin CPU 2.3% | 2.55 | 3.93 | 0.022 |
| process, Python, in bwrap | 2 | 1.77 | 17.8 | 7.4 | 25.3 | 1,178 | plugin CPU 2.5% | 2.89 | 4.26 | 0.022 |
| process, C++ | 2 | 1.74 | 17.7 | 7.4 | 25.6 | 1,179 | plugin CPU 0.3% | 0.66 | 1.67 | 0.022 |

## Misbehaving plugins

One layer each, 200 objects. The plugin misbehaves after 60 updates (about 6 s); the flood starts at once. *t* is seconds since the viewer started (measurement starts at 3 s). *Overlays/s* is how many overlays reached the GUI thread per second.

| Plugin | Viewer stalled | GUI lag p99 | GUI lag max | GUI CPU | Process CPU | Overlays/s | First non-ok | Restarts | Final | Why |
|---|---|---|---|---|---|---|---|---|---|---|
| item, endless loop | **stalled** | 2.81 | 4.5 | 79.9 | 91.9 | 0 | – | 0 | ok |  |
| script on GUI thread, endless loop | no | 2.91 | 119.1 | 7.0 | 28.3 | 2 | stopped at 6.27 s | 0 | stopped | update() ran for 101 ms; interrupted |
| script on own thread, endless loop | no | 2.76 | 11.2 | 6.4 | 28.2 | 2 | stopped at 6.26 s | 0 | stopped | update() ran for 101 ms; interrupted |
| script on GUI thread, 30 ms per update | no | 2.79 | 4.4 | 6.4 | 27.3 | 0 | suspended at 0.39 s | 0 | suspended | 30.3 ms per update on the GUI thread (budget 4 ms) |
| script on own thread, 30 ms per update | no | 3.03 | 13.0 | 7.3 | 58.9 | 10 | – | 0 | ok |  |
| process, crashes | no | 3.05 | 11.1 | 6.4 | 27.9 | 9 | crashed at 6.33 s | 2 | ok | crashed; restarting in 250 ms |
| process, stops answering | no | 2.76 | 9.2 | 6.6 | 27.9 | 7 | not responding at 8.20 s | 2 | ok | no answer for 2 s; restarting in 250 ms |
| process, writes garbage | no | 2.92 | 9.8 | 6.7 | 27.6 | 9 | error at 6.16 s | 2 | ok | protocol error: not an overlay stream; restarting in 250 ms |
| process floods; every overlay handed over, all decoded | no | 3.47 | 11.1 | 19.8 | 141.6 | 19,524 | – | 0 | ok |  |
| process floods; newest decoded, hand-overs paced | no | 3.76 | 12.9 | 7.6 | 130.6 | 124 | – | 0 | ok |  |
| process floods; one answer per input enforced | no | 2.82 | 5.6 | 6.2 | 27.0 | 0 | error at 0.18 s | 4 | failed | flooding: 51 answers for 0 inputs; restarting in 250 ms |

### Status timelines (first run of each)

**item, endless loop**

```
  0.138 s  ok              in the viewer's engine
```

**script on GUI thread, endless loop**

```
  0.141 s  ok              own QML engine, GUI thread
  6.274 s  stopped         update() ran for 101 ms; interrupted
```

**script on own thread, endless loop**

```
  0.119 s  ok              own QML engine, own thread
  6.262 s  stopped         update() ran for 101 ms; interrupted
```

**script on GUI thread, 30 ms per update**

```
  0.144 s  ok              own QML engine, GUI thread
  0.392 s  suspended       30.3 ms per update on the GUI thread (budget 4 ms)
```

**script on own thread, 30 ms per update**

```
  0.122 s  ok              own QML engine, own thread
```

**process, crashes**

```
  0.159 s  ok              pid 3863890 · misbehave crash
  6.332 s  crashed         crashed; restarting in 250 ms
  6.602 s  ok              pid 3864269 · misbehave crash
 12.733 s  crashed         crashed; restarting in 500 ms
 13.251 s  ok              pid 3864674 · misbehave crash
```

**process, stops answering**

```
  0.160 s  ok              pid 3864963 · misbehave hang
  8.204 s  not responding  no answer for 2 s; restarting in 250 ms
  8.473 s  ok              pid 3865392 · misbehave hang
 16.403 s  not responding  no answer for 2 s; restarting in 500 ms
 16.923 s  ok              pid 3865868 · misbehave hang
```

**process, writes garbage**

```
  0.160 s  ok              pid 3865930 · misbehave garbage
  6.164 s  error           protocol error: not an overlay stream; restarting in 250 ms
  6.436 s  ok              pid 3866287 · misbehave garbage
 12.464 s  error           protocol error: not an overlay stream; restarting in 500 ms
 12.982 s  ok              pid 3866689 · misbehave garbage
```

**process floods; every overlay handed over, all decoded**

```
  0.169 s  ok              pid 3866965 · misbehave flood
```

**process floods; newest decoded, hand-overs paced**

```
  0.214 s  ok              pid 3868019 · misbehave flood
```

**process floods; one answer per input enforced**

```
  0.182 s  ok              pid 3868980 · misbehave flood
  0.182 s  error           flooding: 51 answers for 0 inputs; restarting in 250 ms
  0.434 s  ok              pid 3869018 · misbehave flood
  0.439 s  error           flooding: 51 answers for 0 inputs; restarting in 500 ms
  0.960 s  ok              pid 3869059 · misbehave flood
  0.964 s  error           flooding: 51 answers for 0 inputs; restarting in 1000 ms
  1.986 s  ok              pid 3869165 · misbehave flood
  1.990 s  error           flooding: 51 answers for 0 inputs; restarting in 2000 ms
  3.916 s  ok              pid 3869261 · misbehave flood
  3.922 s  failed          flooding: 51 answers for 0 inputs; 5 times in a minute, not restarted
```

### Watchdog and restart times (p50 per run)

| Case | Values (ms) |
|---|---|
| spin-js-gui: interrupted_after_ms | 101, 105 |
| spin-js-worker: interrupted_after_ms | 101, 102 |
| crash: restart_ms | 394, 394 |
| hang: restart_ms | 394, 393 |
| garbage: restart_ms | 395, 393 |
| flood-contract: restart_ms | 1926, 1990 |
