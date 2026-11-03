# Step 2: what level of detail costs the picture

Frozen scene, 4 × 600k points, no objects. Lit = differs from the background by more than 40 (RGB distance). Agreement allows 1 pixel of positional tolerance.

| camera distance m | LOD stride | lit px, all points | lit px, LOD | lit ratio | LOD px near a full px | full px near a LOD px |
|---|---|---|---|---|---|---|
| 90 | 1 | 291,326 | 291,326 | 1.0 | 1.0 | 1.0 |
| 300 | 4 | 43,025 | 35,145 | 0.8 | 1.0 | 1.0 |
| 500 | 8 | 15,484 | 13,416 | 0.9 | 1.0 | 1.0 |
