# Experiment 05: paused video seek

**Question:** how long does `QMediaPlayer` take to show the exact frame at a new position while paused, and what decides it?

**Setup:** `e05_video_seek <video> <decoders> <seeks> <gop>`: N paused players on the same file, each seeking to random whole frames, 50 ms apart. One CSV row per seek: target, time since the previous keyframe, latency (`setPosition()` to the frame in the sink), frame shown.

**Run:** `python3 scripts/video_seek.py` (makes the 1 s, ⅓ s and all-intra test videos, runs 1 and 4 decoders, writes `results/step-04/video-seek.md`).

**Answer:** a seek decodes from the previous keyframe: ~13 ms at a keyframe plus ~0.75 ms per frame after it. Four decoders at once double it. A keyframe every ⅓ s takes four-decoder seeks from 54/105 ms (p50/p99) to 27/44 ms for 8% more file. Used by step 4.
