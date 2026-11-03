# Replay parity, pixel by pixel

`scripts/parity.py replay` (1-pixel tolerance) passes for both encodings, and its negative control (the live scene with seed 2, which the replays run alongside) fails as it must: [`parity.md`](parity.md).

How exact is "pass"? Exact pixel counts in the parity region (1220 × 680, `compare -metric AE`), OpenGL:

| Pair | Differing pixels |
|---|---|
| live run 1 vs live run 2 | 124 |
| live run 1 vs live run 3 | 128 |
| live run 2 vs live run 3 | 66 |
| live (parity run) vs **float32 replay** | 126 (Vulkan: 120) |
| live (parity run) vs **int16 replay** | 2,574 (Vulkan: 2,444) |

- Every pixel that differs between any two of four runs (three live, one float32 replay) differs by 1 in one colour channel and takes **exactly two values** across the runs (228 pixels, all with 2 versions). That is the renderer's own run-to-run variation (overlapping points blended in a varying order), present before the timeline existed.
- **float32 replay is as exact as live is to itself.**
- **int16 replay** moves points by up to 5 mm. At 7 px/m that crosses a pixel boundary for 0.3% of lit pixels: 20× the noise floor, invisible at this zoom (precision and recall 1.0 with 1-pixel tolerance).
