# ARCHITECTURE.md — project map

A map for the agent: where the layers live, who talks to whom, and where new code goes. The source of truth
is the code and its behavior (`CLAUDE.md`); the map may lag behind — edit it together with behavior. Russian
version — [ARCHITECTURE.md](ARCHITECTURE.md); both are one source, edit them in the same pass.

A deep dive into the viewer (layers, threads, QML contract) is in [DESIGN.md](DESIGN.md). This file is the map
of the whole product; a new directory, layer, or component is justified here.

## 1. Product

The product is four independently versioned projects (manifest — [projects.json](projects.json)):

| id | Path | Kind | Entry point |
|---|---|---|---|
| `react-interface` | `apps/react-interface` | Next.js + TS | `app/page.tsx` |
| `qt-interface` | `apps/qt-interface` | QML | `main.qml` |
| `gcode-viewer` | `modules/gcode-viewer` | QML + C++ | `GcodeViewer3D.qml` |
| `cad-testdata` | `data/cad-testdata` | data | `Test_model.ngc` |

How the projects connect: `cad-testdata` provides the NC program and postprocessor, `gcode-viewer` reads them
through the `UI_*` tag contract; `qt-interface` mirrors the `react-interface` UI word for word. `version_probe`
checks the versions.

## 2. `react-interface` (operator panel)

Layers, top to bottom:

* `app/` — routes and entry point: `page.tsx` (panel), `classic/` (the `/classic` route — same reducer, always
  the panel variant), `layout.tsx`, `globals.css`.
* `components/cnc/` — the view layer: screens (`screens/`), the physical console (`hardware/`), dialogs,
  overlays, `primitives.tsx`. Touch variant — `components/cnc/touch/`; switch — `ui-mode.tsx`, context —
  `ui-mode-context.ts`.
* `lib/cnc/` — **pure machine logic, no JSX, React, or DOM**: `machine.ts` (reducer that composes the
  subsystems through a registry), `subsystem.ts` (the action layer's leaf: `Action`, `Result`, `Dispatch`,
  the registry entry), and the subsystems next to it (`ladder.ts` is the sample, `ladder-scan.ts` is the
  PLC scan,
  `nav.ts`, `cycle.ts`, `control.ts`, `service.ts`), `types.ts`, `coords.ts`, `clamp.ts` (list index
  clamping), `keys.ts` (the console key map: `keyToAction` and `routeKey`), `gcode.ts` (block parsing, modal
  codes, program tools), `spindle.ts` (rpm, cutting,
  load), `calc.ts` (cutting-mode math), `guards.ts` (input checks),
  `data.ts` and `softkeys.ts` (texts and icons), `service-data.ts` (the catalogue API: `ParamDef` types and
  the sections), `params/` (the section tables, one file per section: `def.ts`, `display.ts`, `system.ts`,
  `motion.ts`, `linuxcnc.ts`, `mesa.ts`, `joints.ts`, `pid.ts`; a directory of its own so the tables stay
  apart from the logic), `hal.ts` (the HAL pin reference), `messages.ts`, `time.ts` (the stamps from the
  action), `toolpath.ts`, `tool-check.ts`,
  `probe.ts`, `mpg.ts`, `workplace.ts`, `files.ts`, `list.ts`, `prog.ts`, `tool-measure.ts` (tool measuring
  with the setter), `machine-branches.ts` (the machine's own keys: alarms, help, theme), theme logic.
* `public/` — images and static assets of the project.
* Tests live next to the logic (`lib/cnc/*.test.ts`), in the same layer as the defect.

Layer rules: a component receives `state + dispatch` and never changes machine state itself; every action is a
typed `Action`; service values go through `ParamDef`. The panel has no external network API: the state machine
is local (when an API request appears, add and name that layer here).

## 3. `qt-interface` (Qt port of the UI)

* `main.qml` — the source of the UI (edited by hand); `preview.qml` — data, also edited by hand (the generator
  was removed together with the Python layer).
* `assets/` — project images; `fonts/` — `Inter`, `JetBrains Mono`, `lucide.ttf`.
* There is no run entry point in the repository: the `qml` runtime does not load the `MockBackend` module.
* Texts match `react-interface` word for word; icons are lucide only, from the bundled font.
* The port lags the React panel by several passes (workplaces, the Work tab, 8 softkeys, the AUTO screen,
  Minimal, touch mode); the plan to catch up is [docs/PORT-QML.md](docs/PORT-QML.md).

## 4. `gcode-viewer` (3D viewer for NC programs)

Layers (in detail — [DESIGN.md](DESIGN.md)):

* `GcodeViewer3D.qml` — the QML layer: scene, camera, materials, mode panel, key handling.
* `cpp/core/` — **pure core without Qt**: NC parser and `UI_*` tags (`gcode_parser`, `ui_tags`), geometry
  (`outline`, `silhouette`, `tangents`, `normals`), packing (`packing`, `path_prep`), STL import, tool and
  collisions (`tooling`, `collision`), stock removal field (`stock_sim`), types (`gv_types.h`), settings
  (`quality.h`).
* `cpp/occt/` — the STEP layer on OpenCASCADE.
* `cpp/qt/`, `cpp/qmlbackend/` — the Qt bridge: `QQuick3DGeometry` geometries and the `MockBackend` QML module
  (controller `GcodeToolpath`, `gv_stockgeom`).
* `cpp/viewer/` — the `gcodeviewer` entry point and live-path probes (`keyprobe`, `stock_bind_probe`).
* `cpp/*_probe.cpp` — probes checked against the `oracle_manifest.json` oracle (`--manifest` key).
* Shaders: `toolpath.*`, `stepledges.*`, `stocksurface.*`, `stockshell.*`.
* `preview_3d.qml` — a 1024x600 `Window` wrapper (data, edited by hand).

Data flow: NC program -> pure core on a worker thread -> signals (`toolpathReady`, `meshReady`,
`loadFinished`) -> controller -> QML -> GPU (QtQuick3D). Objects visible to QML are touched only by the GUI
thread. The contract with `cad-testdata` is the `UI_*` tags; changing a tag means editing the postprocessor and
the parser in the same pass (`VERSIONING.md`, section 7).

## 5. `cad-testdata` (data and postprocessor)

* `data/cad-testdata/Test_model.ngc` — test NC program; `Test_model.STL` — part model.
* `data/cad-testdata/linuxcnc_ui.cps` — postprocessor with `UI_*` tags; `linuxcnc.cps` — base one.

## 6. Where new code goes

* A new panel subsystem — its own module in `lib/cnc/` next to the others (`ladder.ts` is the sample), wired
  into `machine.ts` through it; `machine.ts` composes, it does not absorb.
* A new viewer layer — into the matching `cpp/` directory (`core` — pure, `occt` — STEP, `qt`/`qmlbackend` —
  bridge, `viewer` — entry point); its check is a probe next to it.
* A new directory is justified in this file; file size stays around the 250-line guideline (`CLAUDE.md`,
  section 2), and exceeding it is recorded in [TODO.md](TODO.md).
