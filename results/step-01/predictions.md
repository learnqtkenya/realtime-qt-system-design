# Step 1: predictions (written before the code exists)

## The feature

One product app (`app/`): a top-down lidar view plus a **lab panel** with
- a renderer switch between six strategies (JS Canvas, painted item, worker raster, scene graph, RHI, Quick 3D), live, without restarting,
- load sliders (points per sensor, rate, objects, events per second),
- live charts of frame time, GUI lag, CPU, latency and GPU time.

## What could go wrong, and the guesses

| Question | Guess | How it is checked |
|---|---|---|
| Does each renderer in the app behave like its experiment? | Process CPU, GPU time and latency within ±15% of experiments 01–04 at the same load (except that the lab panel adds a little) | Sweep at nominal and peak per renderer; compare with experiment numbers measured in the same harness |
| What does the lab panel cost? | GUI CPU +1–3 percentage points, GPU +0.05–0.2 ms, no change in frame rate | Same renderer and load, panel charts on vs off (`--no-charts`) |
| Do all six renderers draw the same picture inside the app? | Yes; precision and recall ≥ 0.98 against the app's own worker raster | `scripts/parity.py app` |
| Is a live renderer switch clean? | New content within 0.5 s; no stderr output; RSS after 30 switches within 20 MB of the start | Scripted switch soak (`--cycle-renderers`) |
| Is a live load change clean? | New load on screen within 1 s even at 1M points (cloud pool rebuild ≈ 0.3 s on the simulator thread); the GUI thread never blocks on it | Lag and frame time while the load changes |
| Can the slowest renderers be pushed into an unrecoverable state from the UI? | No: the panel caps points per renderer (Canvas 50k, painted 150k) and the GUI recovers within 2 s after lowering load | Manual check on the virtual desktop |
