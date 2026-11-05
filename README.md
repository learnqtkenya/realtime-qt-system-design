# Real-time Qt system design

The code for the book **System Design for Scalable Real-Time Qt QML Apps**: a sensor viewer built in
eleven steps, with every design decision measured.

![The viewer at step 5: four lidars fused into one view, four camera panels, 400 tracked objects, a live
event log and the lab panel.](results/step-05/screenshot-data-panel.png)

The program is a live viewer for a vehicle's or a robot's sensors: four lidars at 150,000 points each,
ten times a second, four 1080p cameras, hundreds of tracked objects and thousands of events a second,
fused into one picture that you can pause, scrub back through, record, replay, inspect, extend with
plugins, watch from a second process and open in a browser. The book uses it to teach system design:
who owns the data, which thread does the work, what happens when data arrives faster than it can be
drawn, what is kept and in what shape, what crosses a process boundary, what happens when parts fail,
and how a design survives change.

Every claim in the book is a measurement of this code, and every measurement is in this repository,
with the script that made it.

## Following the book

Each chapter describes the program at one tag. Check the tag out and build it to see exactly the code
the chapter shows; every listing in the book names its file, its lines and its tag.

```bash
git checkout step-05
cmake --preset release && cmake --build --preset release
```

| Tag | Chapter | What it adds |
|---|---|---|
| `experiments-complete` | 1–5 | The measurement harness and five experiments as separate programs under `modules/`: requirements, the first implementation, ownership boundaries, the worker pipeline, the GPU renderers |
| `step-01` | 5 | One product with every renderer behind a lab panel |
| `step-02` | 6 | A 3D orbit view, picking, level of detail, input-to-photon latency |
| `step-03` | 7 | Four camera panels |
| `step-04` | 8 | The timeline: history, recording, replay, one presentation clock |
| `step-05` | 9 | The data panel: an objects table and the event log, updated by diffs |
| `step-06` | 10 | Stream health and fault injection |
| `step-07` | 11 | A second viewer over TCP |
| `step-08` | 12 | Overlay layers from plugins: items, scripts and processes |
| `step-09` | 13 | A saved layout, diagnostics, metrics, an installable package, hour-long runs |
| `step-10` | 14 | Eight lidars, operator notes, a memory budget that follows the machine |
| `step-11` | 15 | The browser build, clock probes, credit for slow clients |

`main` is `step-11` with later fixes. The tags never move: the book's listings point at them.

Each step's notes, with its predictions, measurements and the commands that produced them, are in
[`docs/steps/`](docs/steps/). Experiments have their own notes in `modules/` (at `experiments-complete`)
or `experiments/` (from `step-01` on).

## Building

You need:

- **Qt 6.12** for desktop Linux (`gcc_64`) with Qt Quick, Qt Quick 3D, Qt Quick Controls, Qt
  Multimedia, Qt Shader Tools, Qt WebSockets and Qt Network. The online installer offers them all.
- **CMake 3.25** or newer, **Ninja**, and a **C++20** compiler (the measurements used GCC 15.2).
- Development packages for **zstd** and **zlib**.
- **FFmpeg**, to generate the cameras' test videos, and **Python 3** for the measurement scripts.
- Optional: **Bubblewrap** (`bwrap`) for sandboxed plugins; **Emscripten 5.0.5** for the browser
  build; Firefox and `geckodriver` for the browser measurements.

The presets expect Qt in `/opt/Qt/6.12.0/gcc_64`. If yours is elsewhere, pass it once; CMake
remembers it:

```bash
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0/gcc_64
cmake --build --preset release
ctest --preset release
```

Generate the cameras' videos once, then run the viewer:

```bash
./scripts/make_videos.sh
build/release/app/viewer --points 150000 --objects 200 --events 200
```

`build/release/app/viewer --help` lists the options. Every measurement uses the `release` preset;
never measure the `debug` one.

### In a browser

```bash
source ~/emsdk/emsdk_env.sh                       # Emscripten 5.0.5
cmake --preset wasm && cmake --build build/wasm --target viewer
python3 scripts/webserve.py &                     # serves the page with the headers threads need
build/release/app/viewer --set serveWs=7421 &     # a desktop viewer to watch
```

Then open `http://127.0.0.1:8000/app/index.html?a=--no-sim&a=--set&a=connect=ws://127.0.0.1:7421`.

## The repository

| Path | What it holds |
|---|---|
| `app/` | The viewer: C++ domain and presentation layers, QML, the wire protocol, recording, plugins host |
| `common/` | The measurement harness and the sensor simulator, shared by every program |
| `experiments/` | Focused experiments that answered one question each, with their notes |
| `plugins/`, `plugins-lab/` | The shipped overlay layers, and lab plugins that misbehave on purpose |
| `scripts/` | Checks, measurement sweeps and the reports that turn them into tables |
| `results/` | Every measurement the book cites: predictions, raw reports, summaries, screenshots |
| `docs/steps/` | One note per step: the feature, the design question, predictions and results |

## Measuring on your machine

Each step's sweep script is named in its notes. The scripts start the viewer with `--bench`, which
measures after a warm-up and writes a JSON report. The numbers in `results/` come from one machine (a
Ryzen 9 7940HS with its integrated Radeon 780M, 60 Hz, Wayland). Yours will differ; the ratios between
designs should not. If they do, that is worth an issue.

## Contributing

Bug reports, measurements from other machines and fixes are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) first: the tags are fixed points of the book, so changes land on
`main` only. This project follows a [code of conduct](CODE_OF_CONDUCT.md). Security issues go through
[SECURITY.md](SECURITY.md).

## Licence

The code and everything else in this repository are under the [MIT licence](LICENSE). The book is
published separately under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 licence
([CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/)).

Neither Qt nor zstd is part of this repository. zstd (BSD licence) is downloaded at configure time
for the browser build only. Qt comes under its own licences, which differ by module: check them for
anything you ship in binary form. Qt Quick 3D, for one, is available under the GPL 3 or a commercial
licence, not the LGPL.
