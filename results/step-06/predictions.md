# Step 6: predictions (written before the code exists)

## The feature

- **Sensor health** (lab panel): one row per stream (four lidars, tracks, events, four cameras, and the GUI thread itself). Each row shows a status light, the rate, the age of the newest sample, drops, rejected samples and latency.
- **Fault injection** (lab panel, and `--set fault=...` for benchmarks), per stream:
  - drop a share of samples;
  - add delay, or jitter (which reorders samples);
  - corrupt samples (NaN and out-of-range points; NaN track positions);
  - stall (stop sending);
  - block the GUI thread.
- **Health events in the event log** when a stream changes state.
- **Validation at ingest**: bad samples are rejected and counted, so views, history and recordings never see them.

## The design questions

What happens when parts fail, and who finds out?

## Guesses

| Question | Guess |
|---|---|
| Corrupt lidar without validation | No crash. NaN points vanish on the GPU; spikes are off screen; **the int16 recording silently clamps or garbles them**, so the record disagrees with what was shown |
| Corrupt tracks without validation | No crash; the object can't be picked, draws nothing, and the table sorts unpredictably (NaN breaks ordering) |
| Validation cost (finite and range check of 150k points) | 0.1–0.3 ms per frame on the simulator thread (~1% of a core at nominal) |
| Stall detection (lidar, 10 Hz), with "stale" at 5 periods | ~500 ms after the last sample; up to 50 ms more from the 20 ms check interval |
| Delay detection (+300 ms) | Within one sample period of the first delayed sample |
| Drop detection (50%) | Within 1 s (sequence gaps over a 1 s window) |
| Corrupt detection | On the first corrupt sample (validation counts it) |
| GUI blocked 300 ms: who notices? | Only a watcher on another thread. The GUI can't report its own stall; the health thread sees the heartbeat age within ~50 ms |
| False alarms in a 5-minute soak at nominal load | 0 with thresholds at 2× (late) and 5× (stale) the period; a few at 1.5× under peak load |
| The rest of the app while one lidar is stalled, delayed or corrupt | Unaffected: 60 fps, GUI lag within budget (the streams are already isolated by the mailbox) |
| Jitter ±100 ms on one lidar | Out-of-order arrivals; the presenter's per-stream cursor shows only the newest; the recording sees disorder up to 200 ms |
