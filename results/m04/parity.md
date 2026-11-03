# Module 4: pixel parity against the CPU raster (Module 3)

Scene: `--still --points 20000 --objects 50 --events 0 --grab-after 3`. Region x 0-1040, y 170-800. Precision and recall allow 1 pixel of positional tolerance. Pass: both ≥ 0.98, white (track box) pixel count within 10%, no stderr output.

| variant | api | ref lit px | lit px | lit ratio | precision | recall | white ratio | colour error | stderr | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| sg | opengl | 60,976 | 60,623 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| rhi | opengl | 60,976 | 60,623 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| rhi-ring | opengl | 60,976 | 60,623 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| quick3d | opengl | 60,976 | 60,561 | 1.0 | 1.0 | 1.0 | 1.0 | 1.1 | 0 | yes |
| sg | vulkan | 60,977 | 60,583 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| rhi | vulkan | 60,977 | 60,583 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| rhi-ring | vulkan | 60,977 | 60,583 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| quick3d | vulkan | 60,977 | 60,521 | 1.0 | 1.0 | 1.0 | 1.0 | 1.1 | 0 | yes |
