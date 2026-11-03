# Lidar frame encodings (150000 points per frame)

Median over 16 simulator frames (4 sensors × 4 variants), one thread. MB/s are of raw float32 input (1.8 MB per frame).

| Encoding | Bytes per point | Ratio vs float32 | Encode ms/frame | Decode ms/frame | Encode MB/s | Nominal load (40 frames/s): encode CPU |
|---|---|---|---|---|---|---|
| float32 | 12.00 | 1.00 | 0.06 | 0.08 | 29695 | 0.2% of a core |
| float32 + zstd 1 | 10.98 | 1.09 | 2.38 | 1.27 | 756 | 9.5% of a core |
| float32 + shuffle + zstd 1 | 9.49 | 1.26 | 3.03 | 1.40 | 594 | 12.1% of a core |
| int16 (1 cm) | 6.00 | 2.00 | 0.18 | 0.38 | 9809 | 0.7% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + zstd 1 | 4.70 | 2.55 | 1.11 | 0.90 | 1628 | 4.4% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 1 | 3.64 | 3.29 | 2.09 | 1.44 | 862 | 8.4% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 3 | 3.65 | 3.29 | 3.05 | 1.09 | 590 | 12.2% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 9 | 3.56 | 3.37 | 8.46 | 0.95 | 213 | 33.8% of a core |
|   max error 5.00 mm | | | | | | |
| int16 planar (one pass) | 6.00 | 2.00 | 0.20 | 0.25 | 9115 | 0.8% of a core |
|   max error 5.00 mm | | | | | | |
| int16 planar (one pass) + zstd 1 | 3.64 | 3.29 | 1.87 | 0.96 | 965 | 7.5% of a core |
|   max error 5.00 mm | | | | | | |
# Lidar frame encodings (300000 points per frame)

Median over 16 simulator frames (4 sensors × 4 variants), one thread. MB/s are of raw float32 input (3.6 MB per frame).

| Encoding | Bytes per point | Ratio vs float32 | Encode ms/frame | Decode ms/frame | Encode MB/s | Nominal load (40 frames/s): encode CPU |
|---|---|---|---|---|---|---|
| float32 | 12.00 | 1.00 | 0.17 | 0.18 | 20623 | 0.7% of a core |
| float32 + zstd 1 | 10.96 | 1.09 | 4.85 | 2.56 | 742 | 19.4% of a core |
| float32 + shuffle + zstd 1 | 9.48 | 1.27 | 5.99 | 2.80 | 601 | 24.0% of a core |
| int16 (1 cm) | 6.00 | 2.00 | 0.36 | 0.77 | 9949 | 1.4% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + zstd 1 | 4.67 | 2.57 | 2.67 | 1.86 | 1348 | 10.7% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 1 | 3.62 | 3.31 | 4.18 | 2.97 | 860 | 16.7% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 3 | 3.63 | 3.31 | 5.63 | 2.95 | 639 | 22.5% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 9 | 3.54 | 3.39 | 17.35 | 1.89 | 207 | 69.4% of a core |
|   max error 5.00 mm | | | | | | |
| int16 planar (one pass) | 6.00 | 2.00 | 0.40 | 0.50 | 9105 | 1.6% of a core |
|   max error 5.00 mm | | | | | | |
| int16 planar (one pass) + zstd 1 | 3.62 | 3.31 | 3.68 | 1.91 | 980 | 14.7% of a core |
|   max error 5.00 mm | | | | | | |
