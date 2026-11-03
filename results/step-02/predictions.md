# Step 2: predictions (written before the code exists)

## The feature

- **3D orbit view**: drag to orbit, wheel to zoom; points coloured by height; tracked objects as 3D boxes.
- **Click to inspect**, in the top-down view and in 3D: the selected object is highlighted and an inspector shows its id, class, speed, heading, position and distance.
- **Level of detail** in 3D: when the camera is far away, draw every k-th point.

## Design choices being tested

- **Where picking lives.** In the domain: a C++ uniform grid over the tracks, queried with a point (top-down) or a ray (3D), tested against each oriented box. Compared with Quick 3D's built-in `View3D.pick()`, which tests triangles per model.
- **Interaction latency** is a new service level: from an input event to the frame that shows its result. Target: p99 ≤ 50 ms.

## Guesses

| Question | Guess |
|---|---|
| 3D view at nominal (4 × 150k @ 10 Hz) | 60 fps; GPU 1.5–2.5 ms; GUI lag p99 ≤ 6 ms (Quick 3D copies on the GUI thread) |
| 3D view at peak (4 × 300k @ 20 Hz) | 60 fps; GPU 3–4 ms |
| 3D at 4 × 600k @ 20 Hz, camera far, LOD off vs on (stride 4) | GPU 5–6 ms → 1.5–2.5 ms; GUI copy cost about 4× lower; lit pixels differ by < 10% |
| Interaction latency, 3D orbit drag at nominal | p50 25–35 ms, p99 < 50 ms |
| Interaction latency, top-down click with RHI | p50 20–35 ms |
| Interaction latency, top-down click with the painted item | p50 > 100 ms (the GUI thread waits for painting) |
| Grid-index pick time at 2000 objects | < 50 µs |
| `View3D.pick()` time | 0.1–0.3 ms at 50 objects, 1–3 ms at 800 (linear in models) |
| Agreement between the two pickers on clicks aimed at objects | ≥ 99% |
| Memory | Flat; Quick 3D adds 30–60 MB over the top-down RHI view |
