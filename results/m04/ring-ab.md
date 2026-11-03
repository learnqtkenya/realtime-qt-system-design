# Module 4: vertex-buffer ring A/B (hypothesis not supported)

**Hypothesis.** The `rhi` variant's variable `prepare()` cost comes from overwriting a vertex buffer the GPU may still be reading. Rotating three buffers per sensor (`rhi-ring`) should remove the slow runs.

**Method.** Interleaved A/B (rhi, rhi-ring, rhi, …), four configurations, three repeats each, same session, GPU timestamps on. Raw: `raw/ab-*.json`, summary: `ring-ab.json`.

| Config | Variant | `prepare()` p50, median [range] ms | GPU p50 ms | Frames > 20 ms per run |
|---|---|---|---|---|
| peak (4 × 300k @ 20 Hz) | rhi | 0.432 [0.397–1.833] | 2.757 | 0, 5, 2 |
| | rhi-ring | 0.420 [0.413–0.466] | 2.756 | 1, 5, 5 |
| 4 × 600k @ 20 Hz | rhi | 0.927 [0.780–3.540] | 5.702 | 1, 5, 74 |
| | rhi-ring | 0.812 [0.766–3.275] | 5.701 | 2, 5, 3 |
| 4 × 1M @ 20 Hz | rhi | 1.181 [1.069–1.279] | 9.655 | 0, 26, 0 |
| | rhi-ring | 1.167 [1.155–6.024] | 9.656 | 134, 3, 1 |
| Vulkan peak | rhi | 2.248 [2.176–2.376] | 2.874 | 5, 6, 9 |
| | rhi-ring | 2.228 [0.648–2.369] | 2.876 | 1, 4, 18 |

**Result.** Medians are the same within a few percent, and both variants have occasional slow runs (rhi at 600k, rhi-ring at 1M). The ring doesn't remove the variance, so the hypothesis is not supported. GPU time is identical to three decimals in every run, so the variance is on the CPU side of the boundary. Its cause remains unexplained; candidates not yet tested are driver-side staging allocation and CPU frequency changes (the governor is `powersave`). The `rhi-ring` code stays as a switchable option, but it isn't the default.
