# Writing an overlay layer

A layer draws over the viewer's top-down view: lines and labels in world metres (x right, y up, the sensor at the origin). The viewer finds layers at start in `plugins/` next to its executable (`--set pluginDir=<dir>[,<dir>]`). Each layer is a directory with a `plugin.json`. Layers are off until the user turns them on in the lab panel (or `--set layers=<id>,<id>`).

## The manifest

```json
{
    "id": "speed-vectors",
    "name": "Speed vectors",
    "description": "Where each object will be in 2 s",
    "kind": "script",
    "api": "1.0",
    "entry": "layer.mjs"
}
```

| Field | |
|---|---|
| `id` | Lower-case letters, digits and dashes; unique |
| `kind` | `script` (recommended), `process`, or `item` |
| `api` | The API version the layer was written against, `major.minor`. The viewer refuses a layer whose major differs from its own or whose minor is newer. The viewer's version is shown in the lab panel. |
| `entry` | `script`: a JavaScript module (`.mjs`) or a QML file. `item`: a QML file |
| `exec` | `process`: the program and its arguments; relative paths are relative to the layer's directory |
| `properties` | Passed to the layer: `init(properties)` for a module, initial properties for QML, the hello's `properties` for a process |

## API 1.1

Every update gets the tracks currently shown, and returns what to draw for them.

| Input | Since |
|---|---|
| `tracks`: `id`, `x`, `y` (m), `heading` (rad, 0 = +x, counter-clockwise), `speed` (m/s), `length`, `width` (m), `cls` (0 car, 1 truck, 2 cyclist, 3 pedestrian) | 1.0 |
| `t`: the tracks' capture time (ms) | 1.0 |
| `selectedId`: the selected object, or -1 | 1.1 |

| Drawing | |
|---|---|
| `color("#rrggbb" or "#aarrggbb")` | For what follows |
| `line(x1, y1, x2, y2)` | |
| `circle(x, y, r)` | 48 segments |
| `text(x, y, string)` | Up to 256 characters |

Limits per update: 50,000 segments, 1,000 labels. Calls beyond them are dropped and counted.

## Script layers

```js
export function update(input, draw) {
    for (const t of input.tracks)
        draw.line(t.x, t.y, t.x + 2 * t.speed * Math.cos(t.heading), t.y + 2 * t.speed * Math.sin(t.heading));
}
```

- **Engine:** each script layer runs in its own JavaScript engine, on its own thread. A module gets nothing but its arguments and `console`: no `Qt`, no network, no files. A QML entry gets a QML engine instead, which can reach more (see `results/step-08/trust.md`).
- **Watchdog:** an `update()` that runs longer than 100 ms is interrupted and the layer is stopped.
- **Skipped updates:** if updates arrive faster than the layer finishes them, it gets the newest and the rest are skipped. The lab panel shows the layer as *slow*.

## Process layers

Any language. The viewer starts the program, writes to its stdin and reads its stdout.

- **Viewer → layer:** a hello, then records:
  - **tracks:** type 2;
  - **selection:** type 7, an `i32`.
- **Layer → viewer:** a hello, then **exactly one overlay record (type 6) for every tracks record**, with the same `t`, even if it draws nothing.
- **Framing:** see `sdk/python/qpwire.py` (Python) and `sdk/cpp/qpwire.hpp` (C++, no Qt). Both are complete: a layer is one `update` function.
- **Forward compatibility:** skip record types you don't know, and read items using the stride the record gives. That is how a layer written for 1.0 keeps working with a newer viewer.
- **Failures:** the viewer restarts a layer that crashes, after 250 ms, doubling up to 4 s. It gives up after 5 crashes in a minute. A layer that doesn't answer for 2 s is killed and restarted.
- **Floods and garbage:** a layer that sends many more answers than it got inputs is cut off as flooding. One that writes anything but records is cut off for a protocol error.
- **Sandbox:** with `--set sandbox=1` the layer runs in `bwrap`:
  - the file system is read-only;
  - home and `/tmp` are empty;
  - there is no network;
  - only the plugins directory is visible.
- **Diagnostics:** stderr lines appear in the viewer's event log.

## Item layers

A QML `Item` placed in the viewer's own scene, with `required property var tracks` (the viewer's tracks model) and `required property real pxPerMetre`. It draws however it likes, on the GUI thread, inside the viewer's engine. It can reach everything the viewer's QML can, and nothing can stop it if it misbehaves. Use it only for code you would merge into the viewer itself.
