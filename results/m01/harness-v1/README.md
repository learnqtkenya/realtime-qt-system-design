# Sweep with harness v1 (superseded)

Kept for the record. Two harness faults make parts of it misleading:

1. The scene only rendered when data changed, so "frames over 20 ms" counted idle gaps as misses (the simulator-off control showed 1 fps).
2. CPU and RSS were sampled by a timer on the GUI thread, so heavy runs got 1-3 samples. 150k points needed a forced exit (exit 4).

Stage timings (ms per second) are unaffected and agree with the re-run.
