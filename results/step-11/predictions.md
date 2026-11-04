# Step 11: predictions (written before the changes)

## The requirement

The viewer runs in a browser tab and shows the live scene served by a desktop viewer:
- **Build:** the same QML and C++, built with Qt 6.12's multithreaded WebAssembly kit.
- **Browser:** Firefox on this machine (Chrome is not installed).
- **What the browser can't do** is switched off at build time, not reimplemented.

## What the browser takes away

| Desktop assumption | In the browser | Expected consequence |
|---|---|---|
| TCP sockets (the step-7 wire) | WebSockets only | The server gains a WebSocket endpoint carrying the same records; the parser is unchanged (it takes bytes, not messages) |
| The TCP backlog is the client's credit (step 7) | The browser reads every message as it arrives and queues it for the page | A slow tab's backlog moves from the server into the browser. The server's backlog stays near zero while the tab's memory and latency grow. A fix needs credit at the application level |
| zstd on the wire | Not in Qt's WASM kit | Compile zstd into the page, or send uncompressed: 4 × 150k at 10 Hz is ~36 MB/s uncompressed vs ~22 MB/s compressed, which is fine on localhost |
| Processes (process plugins, `bwrap`) | None | Process plugins are unavailable; script (`.mjs`) and item layers work |
| Threads | Web Workers on shared memory; the page needs cross-origin isolation headers | Pipeline workers, the health thread and script layers run; the page is served with COOP/COEP |
| `/proc`, cgroups, `mallinfo2` | None | The history budget falls back to its minimum unless it follows the WASM heap limit (a 4 GB address space) |
| Files: layout, log, bundle, recordings | An in-memory filesystem, gone on reload | As built, the layout doesn't survive a reload |
| Signals, the metrics port | None | Not applicable in a tab |
| OpenGL 4.x / Vulkan, GPU timestamps | WebGL 2 (GLES 3.0), no timestamp queries | RHI and Quick 3D renderers work; the GPU-time column is empty |
| FFmpeg and VA-API cameras | Browser video elements | Cameras are the heaviest part: every frame is copied into a WebGL texture |
| `exec()` returns at exit | It never returns | Code after `exec()` never runs: the debounced layout save covers the layout; no bundle on exit |

## Performance

Nominal load (4 × 150k @ 10 Hz, 200 objects, 200 events/s), cameras off, Firefox on this machine:

| Guess | |
|---|---|
| Download (wasm and data) | 30–60 MB; first load 2–5 s from localhost |
| Frame p99 | ≤ 20 ms with the RHI renderer; the JS Canvas and painted renderers much worse |
| GUI lag p99 | **4–8 ms, over budget.** One main thread serves the browser, the WebSocket messages and Qt's event loop, and WASM runs ~1.5× slower than native |
| Lidar frames shown | 35–40 of 40 a second |
| Capture → photon latency p50 | 40–60 ms (the native client: ~27 ms) |
| A background tab | Rendering stops but messages keep arriving: memory grows until the tab is in front again (as built) |

## The WebSocket endpoint, measured natively first

| Guess, loopback, native client, nominal load | |
|---|---|
| Throughput | The same records as TCP: ~22 MB/s with zstd |
| Capture → photon latency | Within 1 ms of TCP: framing is a few bytes per record |
| A slow native WebSocket client (`readMbps`) | Pushes back like TCP (the native socket is read only when the client is ready), so this case can't be measured without a browser |
