# Pixel parity: every variant against `live`

Scene: `--still --points 20000 --objects 50 --events 0 --grab-after 3`. Region x 0-1220, y 170-850. Precision and recall allow 1 pixel of positional tolerance. Pass: both ≥ 0.98, white (track box) pixel count within 10%, no stderr output.

| variant | api | ref lit px | lit px | lit ratio | precision | recall | white ratio | colour error | stderr | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| replay-float32 | opengl | 62,380 | 62,380 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| replay-int16planar | opengl | 62,380 | 62,411 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| live-seed2 (must fail) | opengl | 62,380 | 63,852 | 1.0 | 0.8 | 0.8 | 0.9 | 0.3 | 0 | no |
| replay-float32 | vulkan | 62,335 | 62,335 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| replay-int16planar | vulkan | 62,335 | 62,495 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| live-seed2 (must fail) | vulkan | 62,335 | 63,856 | 1.0 | 0.8 | 0.8 | 0.9 | 0.3 | 0 | no |
