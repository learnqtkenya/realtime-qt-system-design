# Module 4: predictions (written before the renderers exist)

## The change

Points stop being rasterised on the CPU. The GPU draws them as point primitives from the raw `x,y,z` floats (12 bytes per point, no conversion). Three ways to do that are built and compared (checkpoint A):

| Variant | How points reach the GPU | Which thread copies | GUI thread involved? |
|---|---|---|---|
| `sg` | `QSGGeometryNode` per sensor with a custom material (point-size shader) | Render thread, inside sync: `memcpy` into `QSGGeometry` | Blocked during sync (the copy) |
| `rhi` | `QSGRenderNode` per item; own `QRhiBuffer` per sensor, uploaded in `prepare()` | Render thread, after sync | No |
| `quick3d` | `QQuick3DGeometry` per sensor in a `View3D` with an orthographic camera | GUI thread: `QByteArray` copy in `setVertexData()` | Yes (the copy) |

Data path for all three: simulator thread → latest-frame mailbox per sensor → item update. There is no worker stage anymore: there's nothing left to compute.

## Baseline (Module 3, same harness, measured just now)

At 150k points per sensor: 60 fps, process CPU 60%, latency p50 34 ms, GPU 0.41 ms/frame (OpenGL), 0.51 ms (Vulkan).

## Guesses

| Quantity at 150k (nominal) | Module 3 | `sg` | `rhi` | `quick3d` |
|---|---|---|---|---|
| fps | 59.9 | 60 | 60 | 60 |
| Process CPU | 60% | 15–25% | 12–20% | 20–35% |
| GUI lag p99 | 5.4 ms | 5–6 ms | 5–6 ms | 5–7 ms |
| Sync p99 | 1.5 ms | 0.5–2 ms | < 0.5 ms | 1–3 ms |
| GPU per frame | 0.41 ms | 0.5–1.5 ms | 0.5–1.5 ms | 0.8–2.5 ms (extra offscreen pass) |
| Latency p50 | 34 ms | 20–30 ms | 20–30 ms | 20–35 ms |
| RSS | 186 MB | 150–200 MB | 150–200 MB | 200–300 MB (Quick 3D runtime) |

Peak (4 × 300k at 20 Hz): all three hold 60 fps; GPU 1–3 ms per frame.

**Where they break.** Stress runs go to 4 × 600k and 4 × 1M points at 20 Hz (48M and 80M points/s, 0.6 and 1.0 GB/s of point data). Guess: `rhi` holds 60 fps at both; `sg` holds 60 at 600k and shows longer syncs at 1M; `quick3d` breaks first, because every byte is copied on the GUI thread before Quick 3D copies it again.

## Correctness guesses

Using `--still` (same cloud every frame, no motion) and `--grab`, each GPU variant's picture is compared with Module 3's `QPainter` picture. Pixels lit by sensor colours must match within 1 pixel: precision and recall ≥ 0.98.

Assumptions most likely to be wrong, and therefore tested rather than trusted:

- The orthographic camera's magnification is pixels per scene unit (so `pxPerMetre` maps directly). The documentation doesn't say.
- `gl_PointSize = 1.5` covers a footprint comparable to `fillRect(x, y, 1.5, 1.5)`.
- The scene graph's projection matrix needs no extra Y flip in the `rhi` variant.
