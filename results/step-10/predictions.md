# Step 10: predictions (written before the changes)

## The review

Three requirements the design never saw, applied to the finished viewer (`step-09`). For each:
- **Before:** which parts I expect to change, and what I expect to hold.
- **After:** the smallest change that meets it, measured, and the actual diff, compared with the guess.

The score is how well the design's boundaries predicted the change. A good design makes the guess right and the change small.

### A. Eight lidars instead of four (scale out)

- **The requirement:** a bigger vehicle, the same viewer. 8 × 150k points at 10 Hz (12M points/s, twice nominal).
- **Expected to change:**
  - the simulator's sensor mounts;
  - the option limit;
  - the stream numbering used by health, faults and recording (lidar streams are 0–3 and tracks is 4);
  - the sensor colour tables in the renderers and the QML;
  - the lab panel's health list.
- **Expected to hold:** pipeline, mailbox, presenter, history, recording format, wire, plugins. They take a sensor id, not a count.

| Guess | |
|---|---|
| Files changed | 8–12, none of them in the pipeline, history, recording or wire |
| GUI lag p99, 8 lidars, RHI | Within 0.5 ms of 4 lidars: the GUI thread never touches points |
| Frame p99 | Still under 20 ms; GPU time doubles (~0.6 ms) |
| Capture → photon latency | Unchanged: per-sensor latest-wins, 4 workers |
| History | Same byte budget, half the seconds |
| Recording | ~44 MB/s at nominal ×2; the writer keeps up |
| Second viewer over the wire | ~44 MB/s with zstd; one client fine |

### B. Operator annotations (evolve the formats)

- **The requirement:** the operator marks an object with a note ("pedestrian about to cross"). The mark has to be:
  - shown in both views, in the event log, and on a second viewer;
  - recorded, and replayed at the right time.
- **The design:** a new record type in the recording and on the wire; a new event kind.
- **Expected to hold, because readers skip what they don't know:**
  - recordings made before annotations replay in the new viewer;
  - the new viewer's recordings replay in the old viewer (`step-09`), without the annotations;
  - an old viewer connected to a new one works, without the annotations.

| Guess | |
|---|---|
| Files changed | 8–10: codec, recording reader/writer, presenter, wire, timeline, a model and QML |
| Old viewer, new recording | Plays; annotations skipped, counted as unknown records |
| New viewer, old recording | Plays |
| Old client, new server | Connected; annotations ignored |
| New client, old server | Connected; no annotations to show |

### C. A two-core machine (shrink)

- **The requirement:** the viewer runs on a smaller computer. Simulated by pinning the viewer to 2 cores (`taskset`).
- **Expected to change:** nothing. The overload policies (latest-wins per sensor, bounded queues, workers that drop the oldest) should spend fewer lidar frames rather than miss display frames.

| Guess at nominal, 2 cores | |
|---|---|
| Frame p99 | Under 20 ms |
| GUI lag p99 | Under 4 ms, maybe at the edge |
| Lidar frames shown | Fewer: some dropped by the pipeline, counted |
| Capture → photon latency | Higher, still under 100 ms |
| With four cameras as well | Over budget: video decode competes for the same two cores |
