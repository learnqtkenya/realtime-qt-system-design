# Step 9: predictions (written before the code exists)

## The feature

- **Saved layouts:** the viewer remembers how it was left and comes back the same at the next start. That covers:
  - window size, the view (top-down or orbit) and the renderer;
  - which panels are shown;
  - which layers are on;
  - the presentation delay.

  A settings file that is damaged, or written by a newer version, never stops the viewer from starting.
- **Diagnostics:** a "Diagnostics" section in the lab panel shows:
  - build identity (git version, Qt, graphics API);
  - uptime, memory, threads, open files;
  - the logs.

  A button saves a **diagnostic bundle**: one JSON file with everything someone else needs to understand a problem report. A metrics endpoint (`--set metricsPort=<port>`, localhost only) serves the same numbers as text, so a script can watch a long run from outside.
- **Logs:** warnings and state changes go to a JSON-lines log file, rotated by size.
- **A packaged build:** `cmake --install` plus Qt's deploy script give a directory that runs without `/opt/Qt`, from any path.
- **A long soak:**
  - **duration:** an hour;
  - **load:** nominal lidar, four cameras, three layers on, and a second viewer connected;
  - **observer:** a script that watches through the metrics endpoint.

## The design question

How is it shipped, observed and kept healthy for hours?

## Guesses

| Question | Guess |
|---|---|
| Restoring settings at start | < 5 ms; invisible next to start-up |
| A settings file of random bytes, or from version 99 | Starts with defaults (random) or with the keys it knows (newer), and says so in the log |
| Cost of serving metrics once a second | Not measurable in GUI lag (< 0.1 ms p99); < 0.5% CPU |
| Package size (Qt libraries, QML modules, plugins, FFmpeg) | 150–250 MB uncompressed |
| Start-up to first frame, packaged vs build directory | Same within 10%; ~300–600 ms warm, more on first start after boot |
| Packaged app with `/opt/Qt` hidden | Runs; nothing resolves outside the package except system libraries (GL, X/Wayland, libc) |
| Soak, one hour: RSS | Flat after the history fills (~1 min); < 20 MB drift after that |
| Soak: threads and open files | Flat; any growth means a lifetime bug |
| Soak: GUI lag p99 per minute | Stays under 4 ms; no trend |
| Soak: the second viewer | Stays connected; its latency flat |
| What the soak will find | At least one thing that grows: a counter, a log, a cache with no bound |
