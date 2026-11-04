# Lidar frame encodings (150000 points per frame)

Median over 16 simulator frames (4 sensors × 4 variants), one thread. MB/s are of raw float32 input (1.8 MB per frame).

| Encoding | Bytes per point | Ratio vs float32 | Encode ms/frame | Decode ms/frame | Encode MB/s | Nominal load (40 frames/s): encode CPU |
|---|---|---|---|---|---|---|
| float32 | 12.00 | 1.00 | 0.07 | 0.09 | 25705 | 0.3% of a core |
| float32 + zstd 1 | 10.98 | 1.09 | 2.36 | 1.26 | 761 | 9.5% of a core |
| float32 + shuffle + zstd 1 | 9.49 | 1.26 | 2.97 | 1.37 | 606 | 11.9% of a core |
| int16 (1 cm) | 6.00 | 2.00 | 0.35 | 0.39 | 5088 | 1.4% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + zstd 1 | 4.70 | 2.55 | 1.25 | 0.89 | 1436 | 5.0% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 1 | 3.64 | 3.29 | 2.26 | 1.44 | 796 | 9.0% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 3 | 3.65 | 3.29 | 3.18 | 1.08 | 566 | 12.7% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 9 | 3.56 | 3.37 | 8.67 | 0.95 | 208 | 34.7% of a core |
|   max error 5.00 mm | | | | | | |
| int16 planar (one pass) | 6.00 | 2.00 | 0.38 | 0.37 | 4791 | 1.5% of a core |
|   max error 5.00 mm | | | | | | |
| int16 planar (one pass) + zstd 1 | 3.64 | 3.29 | 2.04 | 1.07 | 882 | 8.2% of a core |
|   max error 5.00 mm | | | | | | |
# Lidar frame encodings (300000 points per frame)

Median over 16 simulator frames (4 sensors × 4 variants), one thread. MB/s are of raw float32 input (3.6 MB per frame).

| Encoding | Bytes per point | Ratio vs float32 | Encode ms/frame | Decode ms/frame | Encode MB/s | Nominal load (40 frames/s): encode CPU |
|---|---|---|---|---|---|---|
| float32 | 12.00 | 1.00 | 0.17 | 0.18 | 21454 | 0.7% of a core |
| float32 + zstd 1 | 10.96 | 1.09 | 4.78 | 2.56 | 753 | 19.1% of a core |
| float32 + shuffle + zstd 1 | 9.48 | 1.27 | 5.94 | 2.76 | 606 | 23.8% of a core |
| int16 (1 cm) | 6.00 | 2.00 | 0.72 | 0.81 | 4979 | 2.9% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + zstd 1 | 4.67 | 2.57 | 3.04 | 1.92 | 1186 | 12.1% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 1 | 3.62 | 3.31 | 4.59 | 2.94 | 785 | 18.4% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 3 | 3.63 | 3.31 | 6.17 | 3.01 | 584 | 24.7% of a core |
|   max error 5.00 mm | | | | | | |
| int16 + shuffle + zstd 9 | 3.54 | 3.39 | 17.74 | 1.91 | 203 | 70.9% of a core |
|   max error 5.00 mm | | | | | | |
| int16 planar (one pass) | 6.00 | 2.00 | 0.75 | 0.76 | 4825 | 3.0% of a core |
|   max error 5.00 mm | | | | | | |
| int16 planar (one pass) + zstd 1 | 3.62 | 3.31 | 4.04 | 2.15 | 891 | 16.2% of a core |
|   max error 5.00 mm | | | | | | |
