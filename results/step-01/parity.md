# Pixel parity: every variant against `worker`

Scene: `--still --points 20000 --objects 50 --events 0 --grab-after 3`. Region x 0-1220, y 170-900. Precision and recall allow 1 pixel of positional tolerance. Pass: both ≥ 0.98, white (track box) pixel count within 10%, no stderr output.

| variant | api | ref lit px | lit px | lit ratio | precision | recall | white ratio | colour error | stderr | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| canvas | opengl | 62,621 | 62,709 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| painted | opengl | 62,621 | 62,709 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| sg | opengl | 62,621 | 62,380 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| rhi | opengl | 62,621 | 62,380 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| quick3d | opengl | 62,621 | 62,314 | 1.0 | 1.0 | 1.0 | 1.0 | 1.1 | 0 | yes |
| canvas | vulkan | 62,709 | 62,709 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| painted | vulkan | 62,709 | 62,709 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| sg | vulkan | 62,709 | 62,335 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| rhi | vulkan | 62,709 | 62,335 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| quick3d | vulkan | 62,709 | 62,270 | 1.0 | 1.0 | 1.0 | 1.0 | 1.1 | 0 | yes |
