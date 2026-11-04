# Step 8: predictions (written before the code exists)

## The feature

- **Overlay layers that are not part of the viewer**, drawn over the top-down view:
  - speed vectors: where each object will be in 2 s;
  - range rings every 10 m;
  - a geofence zone that labels the objects inside it.
- **Where they come from:** each layer is a directory with a manifest (`plugin.json`: id, name, API version, kind, entry point) under `plugins/`, found at start.
- **What the user sees:** a "Layers" section in the lab panel lists every layer with an on/off switch, its kind, its status and what it costs.
- **Bad plugins:** a misbehaving plugin is stopped, restarted or refused, and the reason is shown.

## The design questions

How does the system change without breaking? What does a plugin get to touch, how is it versioned, and what happens when it misbehaves?

## Three ways to load a layer

1. **Item plugin** (the first implementation): a QML file loaded into the viewer's own engine and placed over the view. It gets the tracks model and the scale, and draws however it likes.
2. **Script plugin:**
   - **Shape:** a QML object with `update(input, draw)`. It gets the presented tracks as plain data and returns drawing commands (`draw.color()`, `draw.line()`, `draw.circle()`, `draw.text()`); the viewer draws them.
   - **Engine:** the plugin has its own engine, either on the GUI thread or on its own thread.
   - **Controls:** each call is timed, and a watchdog interrupts calls that run too long.
3. **Process plugin:**
   - **Protocol:** an executable started by the viewer, in any language. It reads hello and tracks records on stdin (the step 7 wire) and writes overlay records on stdout.
   - **Delivery:** latest-wins, with the pipe's backlog as the credit.
   - **Failure handling:** a crash restarts it with backoff; silence kills it and restarts it.
   - **Sandbox:** it can optionally run inside `bwrap`.

The same layer (speed vectors) is written in all of them: an item plugin, a script plugin, a Python process and a C++ process.

## Guesses

All at nominal lidar (4 × 150k at 10 Hz), tracks at 10 Hz, cameras off.

| Question | Guess |
|---|---|
| GUI lag p99, item plugin, 2,000 objects | 8–15 ms (one item per object, as in step 5) |
| GUI lag p99, script plugin on the GUI thread, 2,000 objects | +2–4 ms over no layer (2,000 objects to JS, 2,000 calls back) |
| GUI lag p99, script plugin on its own thread or process plugin | Within 0.5 ms of no layer |
| Tracks shown → overlay on the GUI thread | Same thread: 0. Own thread: 1–3 ms. C++ process: 1–3 ms. Python process: 5–15 ms |
| Frames whose overlay is from older tracks than the boxes | Same thread: none. Elsewhere: the frames until the result lands, ~5–15% |
| Script plugin that loops forever: item / script on the GUI thread / own thread | Item: the viewer hangs for good. GUI thread: one 100 ms stall, then the layer is stopped. Own thread: no stall; stopped at 100 ms |
| Script plugin taking 30 ms per update | GUI thread: lag p99 ~30 ms until it's suspended (3 updates). Own thread: no effect; it keeps up (30 < 100 ms) |
| Process plugin that crashes | Viewer unaffected; restarted within 0.5 s; given up after 5 crashes in a minute |
| Process plugin that hangs | Noticed after 2 s of silence, killed, restarted |
| Process plugin that floods (thousands of overlays a second) | Viewer's GUI unaffected (latest-wins); its reader thread busy; frames over the size cap rejected |
| Process plugin that writes garbage | Protocol error on the first bad record; killed and restarted |
| What an item plugin can do to the viewer | Anything QML can: inject faults, start recordings, read every singleton |
| What a script plugin in its own engine can do | No viewer singletons; network denied by the engine; local file reads off (Qt's default) |
| What a process plugin can do | Anything the user can (read files, connect anywhere) unless sandboxed; in `bwrap` with no network and no home: neither |
| A plugin that needs API 2.0 | Refused with a reason; a 1.0 plugin still works when the viewer offers 1.1 |
