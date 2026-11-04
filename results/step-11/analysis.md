# Step 11 analysis

Firefox 159 (snap) on the same machine, headed, driven by geckodriver; the browser viewer is `build/wasm` (Qt 6.12 `wasm_multithread`, Emscripten 5.0.5, Release). Benchmarks run 20 s after 3 s unless stated. *Heap* in the browser is the WebAssembly memory (grows, never shrinks); on the desktop it is RSS.

## A. The WebSocket endpoint (desktop client, loopback, nominal load)

| Transport | n | Latency p50 ms | Latency p99 ms | Lidar frames/s shown | Received MB/s |
|---|---|---|---|---|---|
| TCP | 3 | 29.3–30.3 | 39.3–39.8 | 40.0 | 21.9 |
| WebSocket | 3 | 29.0–30.1 | 39.8–48.9 | 39.9–40.0 | 21.9 |

## B. Clocks

| Run | n | Latency p50 ms (as the client measures it) | Offset found ms | Round trip of the probe in use ms | Lidar frames/s shown |
|---|---|---|---|---|---|
| Browser client, as built | 3 | 1,793,812,854,649.9–1,793,812,854,650.5 | – | – | 40.0 |
| Browser client, clock probes | 3 | 36.1–37.3 | -1,793,812,854,614.12–-1,793,812,854,613.12 | 0.29–0.32 | 40.0 |
| Desktop client, server clock +5 s, as built | 3 | -4,970.6–-4,970.1 | – | – | 40.0 |
| Desktop client, server clock +5 s, clock probes | 3 | 29.2–29.7 | 5,000.00–5,000.01 | 0.11–0.12 | 40.0 |
| Desktop client, server clock −5 s, as built | 1 | 5,029.5 | – | – | 40.0 |
| Desktop client, server clock −5 s, clock probes | 3 | 29.0–29.3 | -4,999.99–-4,999.78 | 0.09–0.54 | 40.0 |

With a 200 ms presentation delay (desktop client, 10 s after 3 s). As built, frames stamped 5 s in the future wait 5 s to be shown, and the latency the client measures (224 ms) does not show it:

| Server clock | Client | Lidar frames/s shown | Latency p50 ms (measured) |
|---|---|---|---|
| 0 | as built | 40.0 | 225.0 |
| 0 | clock probes | 39.9 | 223.9 |
| +5000 ms | as built | 31.6 | 224.2 |
| +5000 ms | clock probes | 40.0 | 224.4 |
| -5000 ms | as built | 40.0 | 5,031.2 |
| -5000 ms | clock probes | 40.1 | 224.5 |

## C. The main thread

Nominal load, cameras off. In the browser the GUI-lag probe ticks every 4 ms (browsers clamp repeating timers to 4 ms; a 2 ms probe read 2 ms of lag on an idle page).

| Run | n | GUI lag p99 ms | Frame p99 ms | Sync p99 ms | Render mean ms | Labels ms | Track model ms | Latency p50 ms | Lidar frames/s shown | Heap MB |
|---|---|---|---|---|---|---|---|---|---|---|
| Desktop client (WebSocket) | 3 | 2.66–2.81 | 17.6–17.7 | 3.14–3.40 | 0.78–0.87 | 2.13–2.20 | 0.83–0.88 | 29.0–30.1 | 39.9–40.0 | 1,161–1,162 |
| Browser, idle page (no data) | 3 | 3.86–4.10 | 19.9–20.4 | 0.50–0.54 | 0.24–0.25 | – | – | 0.0 | 0.0 | 104 |
| Browser client, no labels (201 objects) | 3 | 5.36–6.22 | 22.2–23.8 | 1.24–1.32 | 1.22–1.25 | 0.39–0.42 | 1.22–1.25 | 36.1–37.8 | 39.9–40.0 | 1,112–1,114 |
| Browser client | 3 | 7.59–8.24 | 24.4–25.5 | 4.46–4.49 | 1.39–1.40 | 2.97–2.98 | 1.17–1.22 | 36.1–37.3 | 40.0 | 1,113–1,115 |
| Browser, own simulator | 3 | 8.08–9.04 | 25.6–26.3 | 4.20–4.26 | 1.32–1.42 | 3.15–3.18 | 1.25–1.28 | 26.9–27.5 | 40.0–40.1 | 1,113–1,114 |

Labels, before and after laying them all out at once (the desktop rows: `native-ws-r1..3` timed the whole overlay sync, which is labels plus 0.03 ms):

| Run | n | GUI lag p99 ms | Frame p99 ms | Sync p99 ms | Render mean ms | Labels ms | Track model ms | Latency p50 ms | Lidar frames/s shown | Heap MB |
|---|---|---|---|---|---|---|---|---|---|---|
| Browser, a layout per label | 3 | 7.51–8.26 | 24.7–25.9 | 4.90–5.18 | 1.32–1.36 | 3.46–3.50 | 1.16–1.23 | 35.9–37.7 | 40.0 | 179 |
| Browser, one layout for all labels | 3 | 7.08–8.50 | 24.2–26.2 | 4.06–4.34 | 1.20–1.21 | 2.79–2.89 | 1.14–1.22 | 36.1–39.4 | 40.0–40.1 | 179 |
| Desktop, a layout per label | 3 | 2.94–3.15 | 17.6–17.8 | 3.55–3.66 | 0.82–0.91 | – | 0.83–0.86 | 28.7–29.8 | 39.9–40.0 | 1,160–1,161 |
| Desktop, one layout for all labels | 1 | 2.82 | 17.8 | 2.98 | 0.80 | 2.06 | 0.82 | 30.5 | 40.0 | 1,158 |

Desktop overlay sync with a layout per label: 2.47–2.49 ms.

## D. A client that falls behind

The client spends 50 ms on every lidar record (`--set clientDelayMs=50`: about half of the 40 a second), 75 s after 3 s; the browser's history pinned to 64 MB. Outside: the serving viewer's backlog for the client and the frames it replaced (its metrics endpoint), and Firefox's tab process (`/proc`). Credit window: 4 MB (the default).

| Client | Latency ms, 3 s → end | Server backlog (max) | Server replaced (total) | Heap MB, start → end | Firefox tab RSS MB | Received MB/s |
|---|---|---|---|---|---|---|
| Desktop TCP, as built | 960 → 569 | 1.1 MB | 1,978 | 357 → 1,231 | – | 10.4 |
| Desktop TCP, credit | 502 → 382 | 4.7 MB | 2,047 | 359 → 1,587 | – | 10.4 |
| Desktop WebSocket, as built | 689 → 473 | 1.1 MB | 1,979 | 365 → 1,163 | – | 10.4 |
| Desktop WebSocket, credit | 261 → 256 | 4.4 MB | 2,053 | 376 → 1,247 | – | 10.4 |
| Browser, as built | 2,833 → 41,091 | 0.0 MB | 0 | 258 → 1,114 | 465 → 1,708 | 135.6 |
| Browser, credit (run 1) | 508 → 334 | 4.4 MB | 2,035 | 179 → 215 | 443 → 579 | 10.1 |
| Browser, credit (run 2) | 511 → 331 | 4.4 MB | 2,073 | 179 → 215 | 443 → 577 | 10.1 |
| Browser, credit (run 3) | 518 → 331 | 4.4 MB | 2,073 | 179 → 215 | 448 → 594 | 10.1 |

The credit window (`--set creditWindowKb`), browser client. Peak load: 4 × 300k @ 20 Hz, 800 objects, 2,000 events/s, 75 s; slow: as above, one run each:

| Window | Peak runs | Peak: lidar frames/s shown (of 80) | Peak: latency p50 ms | Peak: latency max ms | Peak: heap at end MB | Slow client: latency p50 ms |
|---|---|---|---|---|---|---|
| 1 MB | 3 | 47.6–56.9 | 82–86 | 90–115 | 215 | 169 |
| 4 MB | 3 | 68.3–68.6 | 99–102 | 111–123 | 215 | 336 |
| 8 MB | 1 | 67.6 | 131 | 150 | 258 | 556 |
| 1 GB (no limit) | 3 | 70.3–71.1 | 3,787–4,647 | 6,412–7,396 | 928–1,023 | – |

Other ways for a tab to fall behind, with credit (hidden and busy ran with a 1 MB window; the client kept up in both, so the window was not what limited them):

| Browser | Latency ms, 3 s → end | Server backlog (max) | Server replaced (total) | Heap MB, start → end | Firefox tab RSS MB | Received MB/s |
|---|---|---|---|---|---|---|
| Tab hidden 20–50 s | 36 → 38 | 1.1 MB | 0 | 179 → 215 | 478 → 947 | 21.9 |
| Main thread 60% busy 20–50 s | 38 → 40 | 1.1 MB | 0 | 179 → 215 | 474 → 927 | 21.9 |
| Peak load (4 × 300k @ 20 Hz, 800 objects, 2,000 events/s), 4 MB window | 101 → 95 | 4.4 MB | 758 | 215 → 215 | 537 → 897 | 76.9 |

- **hidden:** fps in 22–50 s 0–60; latency in 22–50 s 0–8,041 ms; after 52 s 35–43 ms; lidar frames/s shown 24.3.
- **busy:** fps in 22–50 s 34–46; latency in 22–50 s 50–93 ms; after 52 s 34–43 ms; lidar frames/s shown 40.0.
- **peak:** fps in 22–50 s 45–60; latency in 22–50 s 86–111 ms; after 52 s 95–106 ms; lidar frames/s shown 68.6.

## E. History budget in the browser

Budget = 30% of the smaller of what the browser admits to (`navigator.deviceMemory`; Firefox: nothing) and the 4 GB WebAssembly address space, capped at 1 GB. 90 s run: heap 372 MB at the start, 1,114 MB at 12 s, 1,114 MB at the end; GUI lag p99 7.40 ms; frame p99 25.7 ms; lidar frames/s shown 40.2. As built (no `/proc` in a page) the budget fell back to its minimum, 64 MB.

## F. Start-up (ms from the page's navigation start, localhost)

| Web Worker pool | Visit | n | Page loaded | main() | QML loaded | First frame | First frame median |
|---|---|---|---|---|---|---|---|
| Pool 4 | first visit (fresh profile) | 5 | 398–422 | 464–480 | 636–652 | 686–700 | 695 |
| Pool 4 | reload | 5 | 403–474 | 414–484 | 575–649 | 619–696 | 637 |
| Pool 10 | first visit (fresh profile) | 5 | 431–441 | 490–499 | 673–678 | 717–730 | 725 |
| Pool 10 | reload | 5 | 422–438 | 436–450 | 600–611 | 645–657 | 653 |

Files: viewer.wasm 38.8 MB, viewer.js 0.3 MB. The desktop viewer's first frame: 250 ms after the process starts (step 9).

## G. Layout across a reload

See [`raw/layout-reload.txt`](raw/layout-reload.txt) and [`layout-reload.png`](layout-reload.png).

## H. Old and new viewers

See [`compat.md`](compat.md).
