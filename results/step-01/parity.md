# Pixel parity: every variant against `worker`

Scene: `--still --points 20000 --objects 50 --events 0 --grab-after 3 --set dataPanel=0`. Region x 0-1220, y 170-900. Precision and recall allow 1 pixel of positional tolerance. Pass: both ≥ 0.98, white (track box) pixel count within 10%, no stderr output.

| variant | api | ref lit px | lit px | lit ratio | precision | recall | white ratio | colour error | stderr | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| canvas | opengl | 70,617 | 55,390 | 0.8 | 1.0 | 0.9 | 1.0 | 6.1 | 0 | no |
| painted | opengl | 70,617 | 70,733 | 1.0 | 1.0 | 1.0 | 1.0 | 4.9 | 0 | yes |
| sg | opengl | 70,617 | 70,444 | 1.0 | 1.0 | 1.0 | 1.0 | 4.9 | 0 | yes |
| rhi | opengl | 70,617 | 70,406 | 1.0 | 1.0 | 1.0 | 1.0 | 4.9 | 0 | yes |
| quick3d | opengl | 70,617 | 70,306 | 1.0 | 1.0 | 1.0 | 1.0 | 5.4 | 0 | yes |
| canvas | vulkan | 70,662 | 55,384 | 0.8 | 1.0 | 0.9 | 1.0 | 6.1 | 8 | no |
| painted | vulkan | 70,662 | 70,696 | 1.0 | 1.0 | 1.0 | 1.0 | 4.8 | 8 | no |
| sg | vulkan | 70,662 | 70,365 | 1.0 | 1.0 | 1.0 | 1.0 | 4.9 | 8 | no |
| rhi | vulkan | 70,662 | 70,331 | 1.0 | 1.0 | 1.0 | 1.0 | 4.9 | 8 | no |
| quick3d | vulkan | 70,662 | 70,338 | 1.0 | 1.0 | 1.0 | 1.0 | 5.5 | 8 | no |
