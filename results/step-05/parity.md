# Pixel parity: every variant against `worker`

Scene: `--still --points 20000 --objects 50 --events 0 --grab-after 3 --set dataPanel=0`. Region x 0-1220, y 170-850. Precision and recall allow 1 pixel of positional tolerance. Pass: both ≥ 0.98, white (track box) pixel count within 10%, no stderr output.

| variant | api | ref lit px | lit px | lit ratio | precision | recall | white ratio | colour error | stderr | pass |
|---|---|---|---|---|---|---|---|---|---|---|
| canvas | opengl | 45,962 | 45,962 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| painted | opengl | 45,962 | 45,962 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| sg | opengl | 45,962 | 45,940 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| rhi | opengl | 45,962 | 45,940 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| quick3d | opengl | 45,962 | 45,921 | 1.0 | 1.0 | 1.0 | 1.0 | 1.1 | 0 | yes |
| canvas | vulkan | 45,962 | 45,962 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| painted | vulkan | 45,962 | 45,962 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| sg | vulkan | 45,962 | 45,936 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| rhi | vulkan | 45,962 | 45,936 | 1.0 | 1.0 | 1.0 | 1.0 | 0.4 | 0 | yes |
| quick3d | vulkan | 45,962 | 45,915 | 1.0 | 1.0 | 1.0 | 1.0 | 1.1 | 0 | yes |
