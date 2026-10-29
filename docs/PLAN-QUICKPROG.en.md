# PLAN-QUICKPROG.md — quick program creation at the control

Draft for roadmap stage M5. The source of truth for behavior is the code and its run; this file holds intent,
boundaries, input forms and work order. Russian original — [PLAN-QUICKPROG.md](PLAN-QUICKPROG.md).

## 1. What we build and what we do not build

**Goal.** An operator creates a short program at the control without CAM and without typing G-code by hand:
pick an operation, enter dimensions, see the toolpath and save `.ngc` for a run.

**First-version boundaries:**

* 3-axis milling, millimetres, `G54`..`G59`, absolute coordinates;
* simple operations: facing, rectangular and circular pockets, drilling and hole grids;
* flat G-code without O-subroutines: the existing viewer understands it and the operator can read it;
* the program model is stored separately from `.ngc`, so an operation can be reopened and edited;
* a run is allowed only after preview and a collision check; there is no automatic start.

**Outside:** full CAM, STEP/DXF import, 3D surfacing, adaptive clearing, islands and rest machining, turning
cycles, automatic feature recognition, custom postprocessors.

**Strategy.** The first vertical slice is a rectangular pocket end to end: form -> `CamPath` -> `emit` -> 2D
preview -> file -> tests. It proves the pipeline without spreading risk across four operations. Facing,
drilling and the operation list follow in the same stage; the document stores an operation array from day one,
even when the first form creates one. A multi-operation program is not deferred "for later": without it quick
programming does not solve a real part.

## 2. What the operator needs

Ordered by frequency and value for a short program at the control.

| Operation | Why the operator needs it | Version | Complexity |
|---|---|---|---|
| Face milling | remove the top, level the stock | MVP | S |
| Rectangular pocket | the most common milling operation | MVP | M |
| Circular pocket | bore for a pin, cup or boss | MVP | M |
| Drilling: single, grid, circle | holes from a drawing | MVP | S-M |
| Contour: closed/open | outside or inside profile | P1 | M |
| Slot | keyway, channel | P1 | M |
| Tapping | `G84` when the machine supports rigid tapping | P1 | M |
| Chamfer/edge break | remove a burr after a contour | P1 | M |
| Array, rotation, operation group | several identical elements on a drawing | P1 | M |
| Custom G-code block | escape hatch for a non-standard operation | P1 | S |
| Counterbore, thread mill, engraving | fasteners, threads, marking | P2 | M |
| Probing block in a program | measure between operations | P1 | M |
| Pocket with island, rest machining | complex parts | P2 | L |
| DXF/STEP import | geometry from a drawing | P2 | L |

No contour in MVP is deliberate: a contour needs lead-in, lead-out and cutter-radius compensation, the main
source of gouges. First prove pockets, facing and drilling on a real part. MVP starts with the pocket end to
end; the other operations follow immediately, not as separate half-products.

## 3. How industrial controls do it

The table records repeated patterns, not marketing claims.

| Control | What it does | What we take |
|---|---|---|
| **Fanuc Manual Guide i** | machining cycles, fixed-form sentences for repeated blocks (tool change, coolant), a figure as a subprogram for roughing/finishing | reusable program preamble, repeated blocks, graphic check |
| **Siemens ShopMill/ShopTurn** | machining-step editor, a separate contour calculator up to 256 elements, pocket/island, roughing and finishing, simulation | operation list, geometry separate from machining, rough/finish passes |
| **Heidenhain TNC** | forms with graphics explaining each parameter; pocket, boss and slot cycles; Klartext | graphic help per field, large forms, cycles |
| **Haas VPS/IPS** | visual templates, standard `G150`, `G12/G13` cycles, graphics | visual operation choice and clear templates |
| **Mazak Mazatrol** | program units: stock, tool, shape, machining; automatic approach/retract | stock and tool first, then operations; a unit model instead of G-code lines |
| **Hurco WinMax** | data blocks, Roughing/Rest/Finishing tabs, `AutoCalc`, patterns, DXF | blocks, tabs, calculated fields, repeat operations |
| **Okuma AOT-IGF** | step-by-step input with interactive graphics and checks | guided wizard and graphics |
| **Tormach PathPilot** | conversational programming on LinuxCNC: drilling, tapping, pockets, operation reorder | the closest architecture: operations -> G-code, not full CAM |
| **NativeCAM** | a separate LinuxCNC add-on: shape catalog, polyline, arrays/rotation, drilling, counterbore, thread mill, engraving, probing, custom G-code | operation catalog, groups and arrays, `Custom G Code`, probing blocks, project order |
| **LinuxCNC NGCGUI/PyNGCGUI** | parameter forms over subroutines, `Create Feature`, `Finalize`, concatenating operations, preview | labels and defaults per parameter, feature list, concatenation, preview |

The common pattern: **stock and tool before operations**, **an operation list**, **a form with graphics**,
**a preview**, and **save to G-code**. None starts the operator with a blank G-code editor.

## 4. What to take from LinuxCNC

### 4.1 NGCGUI/PyNGCGUI

The official LinuxCNC document describes NGCGUI as a Tcl application and PyNGCGUI as its Python version: each
`.ngc` subroutine gives a tab, parameters `#1`..`#30` become fields, special comments set labels and defaults,
`Create Feature` appends an operation, `Finalize` concatenates several operations into one file, and an image
can sit next to the subroutine as help.

What we take:

* **the scheme "operation = parameters + form + help"**;
* **a feature list and its order**;
* **defaults and labels** on every field;
* **preview before save**;
* **concatenating operations into one file**.

What we do not take:

* Tcl/Python code and a dependency on `PyNGCGUI`: Python is forbidden in the project, and GPL LinuxCNC code
  cannot be copied into an Apache-2.0 project;
* O-subroutines in the first version: the viewer and the operator must see flat G-code;
* hand-editing subroutines as the main path.

### 4.2 QtPlasmaC

QtPlasmaC has a "Conversational Shape Library": material selection, shape parameters and a preview. It is not a
milling solution, but the pattern is useful: **cutting data come from material and tool, not from zero every
time**.

### 4.3 NativeCAM

NativeCAM is a separate LinuxCNC add-on, not part of the core. It embeds in AXIS or gmoccapy, ships under
GPL-2.0 and is written in Python/GTK; a Python 3/GTK3 port 2.0b exists. Its operation catalog shows what an
operator really needs:

* **Basic Shapes:** rectangle, circle, circle from two points, slot/oval, radial slot, ellipse, polygon,
  surface finishing;
* **Advanced:** polyline of lines and arcs, mirror, repeat items, counterbore for a socket-head screw, thread
  milling, TrueType engraving, circle with a keyway;
* **Drilling:** single hole, array, regular and irregular hole circle;
* **Groups:** operation group, offset/rotate, radial and rectangular array, A-axis index;
* **Tool Change:** end mill, drill/reamer, thread mill;
* **Probing:** edges, cube/cylinder, point array, surface, hole;
* **Miscellaneous:** stock, custom G-code, insert a G-code file, comment, project notes.

What we take:

* **an operation catalog split into basic and advanced** — the operator is not drowned in a list;
* **groups, arrays and rotation** — common drawing work, not exotica;
* **an explicit tool-change block and probing block**;
* **`Custom G Code` as an escape hatch**, not the main path;
* **a project as an ordered list**, not one huge form.

What we do not take:

* Python/GTK and GPL NativeCAM code: our project is TypeScript/QML/C++ under Apache-2.0;
* "real-time CAM" and hidden automatic decisions;
* the full operation set at once: NativeCAM is a scope benchmark, not a first-version commitment.

A lesson from NativeCAM discussions: implicit operations can produce a formally correct but surprising result.
Therefore every operation shows **an explicit toolpath, depth, tool and warnings**, and `Custom G Code` is
separated from the wizards and needs confirmation.

## 5. Operator flow

1. **Entry.** On the Work screen: "New quick program" or "Open quick program".
2. **Stock.** Confirm `G54`, stock size, top `Z`, safe `Z`, units (mm). Values come from the current state and
   can be taken from the DRO.
3. **Tool.** Choose from the existing tool table: diameter, type, stick-out. Speed, feed and plunge come from
   material and diameter, but remain editable.
4. **Operation list.** Empty list and a large "Add operation" button. Added operations are cards: type, tool,
   size, depth, warnings.
5. **Operation picker.** Tiles with text and icon: "Face", "Pocket", "Circular pocket", "Holes". Not icons
   alone: on P1 the label is mandatory.
6. **Operation form.** Fields on the left, live preview on the right. Tabs: "Geometry", "Tool", "Cutting data",
   "Approach/retract". Each field has a help picture and unit. "Take from DRO" for centre and top. Errors sit
   next to the field, not in one line at the bottom.
7. **Add.** "Add to program" is active only for a valid operation. A refusal shows its reason.
8. **Program.** Cards can be reordered, duplicated, disabled and deleted. Tool changes are grouped; a safe
   retract sits between groups.
9. **Check.** "Check" builds the toolpath, shows stock, estimates time and runs the viewer's simple collision
   check. Errors block saving.
10. **Save.** Two files: `.qprog` — the editable model, `.ngc` — the ready program. The `.ngc` header says
    "generated", the program name and the generator version.
11. **Run.** "Open in editor" or "Run". A start is allowed only after preview, without ESTOP, through the same
    `guards.ts` as a normal program.

### 5.1 Three panel configurations

* **P1, keys only:** focus through fields, `F` keys for "Add", "Check", "Save"; numeric block input; no gestures.
* **P2, keys + touch:** the same actions by touch; large targets; 3D rotation does not disturb the form.
* **P3, touch only:** on-screen keyboard and numeric pad; a step-by-step wizard; labels instead of `F` keys.

## 6. Data model

The program document is not machine state. It can live outside `machine.ts` and connect to the machine only
through "load program", "check", "run".

```ts
// The document saved to .qprog and reopened later.
type QuickProgram = {
  schema: 1
  name: string
  units: 'mm'
  wcs: 'G54' | 'G55' | 'G56' | 'G57' | 'G58' | 'G59'
  stock: StockBox
  safeZ: number
  operations: QuickOperation[]
}

type QuickOperation =
  | FaceOperation
  | RectPocketOperation
  | CirclePocketOperation
  | DrillOperation
  | ContourOperation   // P1
  | SlotOperation      // P1
  | TapOperation       // P1

// Transient input form: strings, cursor, error; not MachineState.
type CamForm = {
  op: QuickOperation['kind']
  values: Record<string, string>
  cursor: number
  preview: CamPath | null
  error: { field: string; reason: string } | null
  writeName: string | null
}

// Planner result: moves, not G-code lines.
type CamMove = {
  kind: 'rapid' | 'cut'
  to: { x: number; y: number; z: number }
  feedMmMin?: number | null
  arc?: { cw: boolean; i: number; j: number }
}

type CamPath = {
  moves: CamMove[]
  bboxMm: { xMin: number; xMax: number; yMin: number; yMax: number; zMax: number }
  estMm: number
  uuid: string
}
```

Common operation fields: `id`, `kind`, `enabled`, `tool`, `spindleRpm`, `feed`, `plungeFeed`, `coolant`,
`topZ`, `depth`, `stepDown`, `finishAllowance`, `notes`. Geometry uses named types (`Point`, `Rect`, `Circle`,
`Polyline`), not tuples. `CamForm` lives outside `MachineState` but reads `isBusy(s)`; `CAM_ACCEPT` converts
form strings to a normalized `QuickOperation`, `CAM_CANCEL` leaves neither preview nor file.

## 7. Architecture

Pure logic lives in `apps/react-interface/lib/cnc/cam/`, without JSX or DOM:

| Module | Responsibility |
|---|---|
| `ops.ts` | `QuickProgram`, operations, `CamForm`, `CamPath`, `CamMove`, `CamContext` |
| `defaults.ts` | defaults from tool and material |
| `validate.ts` | guard clauses and typed `{ ok } \| { bad: { field, reason } }` |
| `toolpath.ts` | planner: `CamPath` from an operation, no G-code strings |
| `feeds.ts` | cutting data through existing `calc.ts`/`spindle.ts`, not a second formula |
| `face.ts`, `pocket-rect.ts`, `pocket-circle.ts`, `drill.ts` | MVP operations |
| `contour.ts`, `slot.ts`, `tap.ts` | P1 operations |
| `planner.ts` | operation order, tool change, safe retract |
| `emit.ts` | flat G-code, header, `UI_*`, footer |
| `serialize.ts` | `.qprog` and schema check |
| `cam.ts` | `CAM_OPEN`, `CAM_OP`, `CAM_PARAM`, `CAM_ACCEPT`, `CAM_CANCEL`, `CAM_PREVIEW`, `CAM_WRITE`; outside `MachineState` but with `isBusy(s)` |

A planner returns `CamPath`, not a string; `emit.ts` prints G-code and the preview draws the same moves. One
geometry source, two representations. `CAM_WRITE` is the only write point; writing goes through `files.ts`, not
a new filesystem. `parseWords` checks syntax but does not replace geometry and the viewer.

UI lives in `components/cnc/cam/`: `cam-screen`, `operation-picker`, `cam-form`, `operation-list`,
`cam-preview`. The screen receives `QuickProgram` and `dispatch`, like other screens. The first form may create
one operation, but the model and list screen are designed for an array from day one.

### 7.1 G-code

Header: `G21 G90 G94 G17 G40 G49 G80`, `G54`, safe `Z`, `T`/`M6`, `S`/`M3`, `M8`.
For the viewer — `UI_STOCK`, `UI_OP`, `UI_TOOL` with the same comments as a normal program.
Footer: `M9`, `M5`, `G0 Z<safe>`, `M30`.

MVP uses the tool centreline, no `G41/G42`: radius compensation and lead-ins are a separate stage, otherwise a
gouge is too easy. Drilling uses `G81`/`G83`; `G84` is P1 and only if the machine confirms rigid tapping. Arcs
use `G2/G3` with `I/J`; a full circle is two half-arcs or `I/J` without an end point (check in the viewer).

## 8. Checks and safety

* Generation is disabled on `ESTOP`, while busy and with an invalid tool table; the form checks `isBusy(s)`.
* Validation returns `{ ok } | { bad: { field, reason } }`; each rule is tied to a field, not one line at the
  bottom. Texts are Russian operator language.
* Checks: the tool fits the pocket (diameter, corner radius), depth stays above the stock bottom, step-down is
  positive, feed and speed stay within machine limits, `WCS` exists, safe `Z` is above the stock, no rapid move
  into material, the operation is not entirely outside the stock, the tool is set.
* Preview is mandatory; the 2D preview is built from `CamPath`, not from a second geometry source. Simple collision goes
  through the viewer; an error blocks saving.
* `CAM_WRITE` is the only write point. Default file name is date + operation; an existing file requires
  confirmation. `CAM_CANCEL` leaves neither preview nor file.
* The first run needs confirmation and never starts automatically.
* Dangerous combinations (tool larger than pocket, thin wall, empty list) are explained in words.
* Preview is recalculated after a 150-200 ms debounce. If generation takes longer than ~100 ms, the planner
  moves to a worker thread; the number is measured, not guessed.

## 7.2 The C++ demo application (Q0, first pass)

The customer wants quick programming exercised on its own, in C++, before it lands in the panel. A separate
application is the only way to do that: the main pipeline is React/TypeScript, the Qt port lags
(`docs/PORT-QML.md`), and C++ is already the project's second native language (the viewer core). The demo
repeats §7 in C++ and is checked from the console, without Qt, so it runs on any machine with a compiler.

**Decision.** A new project `quickprog` — `apps/quickprog` (C++17, standard library only):

* `core/geom.h` — points, rectangle, circle, polyline, rounded rectangle;
* `core/ops.h` — `Program`, `Operation` (the shared fields of §6: tool, feeds, `topZ`, `depth`, `stepDown`,
  allowance) with `Face`, `RectPocket`, `CirclePocket`, `Drill`, `DrillGrid`, `DrillCircle`;
* `core/validate` — the rules of §8 as `Check { field, reason }`: the tool fits the pocket, the depth is not
  below the bottom, the step down is positive, feed and rpm stay inside the machine limits, the safe `Z` is
  above the stock, the operation is not entirely outside the stock;
* `core/feeds` — feeds from diameter, material and tool type on the same numbers as `calc.ts` (one table,
  not a second formula);
* `core/path` — the planner `Operation -> Path` (rapid/cut moves with a feed, no NC lines): facing stripes
  with an overlap, a rectangular pocket as a zigzag with a wall cleanup, a circular pocket as a spiral from
  the centre, hole grids and circles, one drilling cycle per point;
* `core/emit` — `Path -> .ngc`: the header `G21 G90 G94 G17 G40 G49 G80`, `G54`, the safe `Z`, `T`/`M6`,
  `S`/`M3`, `M8`, the `UI_STOCK`/`UI_OP`/`UI_TOOL`/`UI_ORIGIN` tags in comments (the viewer contract,
  `core/ui_tags`), `G81`/`G83` drilling, the tail `M9 M5 G0 Z<safe> M30`;
* `cli/` — the console scenario (`main.cpp` is a thin dispatcher): stock and tool from arguments or
  `--demo`, operations from a `<name>.qprog` file (a flat text format, schema 1), output `<name>.ngc`, a
  printed summary (moves, length, cutting data with its source, time estimate, warnings);
* `tests/` — the project's own check without an external framework: a table of cases, every operation gives
  the expected numbers (pass count, cut area against the square, no cut into the wall for a pocket; 20-30 %
  overlap and retract at the edges for facing; grid and circle coordinates, the `R` plane and the depth, `Q`
  for `G83`); plus a round-trip: the generated NC program is parsed by `gv::parseGcode` from the viewer core,
  the modal codes and the segment count are not zero. Exit code: 0 — everything matched, 1 — a mismatch.

The build skeleton is a `CMakeLists.txt` (the generating part needs neither Qt nor OCCT; `gvcore` is needed
only by the round-trip check, a separate target `quickprog_check`). Linux build —
`scripts/build-quickprog.sh`; Windows — `scripts/build-quickprog.ps1`. Binaries — `build/quickprog/`.
`tools/checks.txt` gets a `quickprog (demo)` line, but only when the build is really available:
`tools/check.sh` knows three requirements (`probe`, `plugin`, `frame`), and a line with another word prints
red. A fourth requirement `quickprog` (the `build/quickprog/quickprog_check` is there) and a step in
`tools/check.ps1` are needed; until then the demo is outside the guard — the record is in `TODO.md`.

**What the demo does NOT do:** no screen and no gestures (that is P1 and the panel), no `.qprog` in the React
format, no run on the machine, no collisions through the viewer. The demo proves the pipeline
`operations -> Path -> .ngc` and hands numbers to the technologist.

**Acceptance of Q0:** a file made for every MVP operation loads in the viewer (`gcodeviewer --smoke` and
offscreen: `loaded OK`, the `UI_*` tags in place); every operation has a case in `tests/` with the pass count
and bounds; the `.ngc` reads by eye and is explained by the technologist; the cutting modes come from the
table and are printed with their source.

## 9. Stages and estimate

Estimates for one strong developer without waiting for a machine; a guide, not a promise.

| Stage | What | Size |
|---|---|---|
| Q0 | operation contract, form mockups, domain review by the technologist, thin screen prototype | S-M |
| Q1 | vertical slice: rectangular pocket from form to `CamPath`, `emit`, 2D preview, file and tests | M |
| Q2 | facing, drilling, operation list and `.qprog`; three operations in one program | M |
| Q3 | viewer check, time, collision, debounce/budget, three configurations | M |
| Q4 | contour, slot, tap, chamfer, arrays/rotation, `Custom G Code`, probing block | M-L |
| Q5 | counterbore, thread mill, engraving, island/rest, DXF | L |
| Q6 | real program and machine run, operator acceptance, docs | M |

MVP Q1-Q3 guide: **6-8 weeks**; P1 Q4: **+6-8 weeks**; P2 Q5: **+8-12 weeks**; polish and machine Q6:
**+2-4 weeks**. The range is refined after the Q0 prototype. Q1 is a vertical slice, not a release: without
Q2 it does not solve a part made of several operations.

## 10. Tests and acceptance

* Each operation gets geometry, depth, step-over, allowance and bounds tests. Pocket: cut area against
  quadrature, pass count, no wall gouge, wall points, plunge.
* Facing: 20-30 % strip overlap, edge retract. Drilling: grid/circle coordinates, `R` plane, depth, `Q` for
  `G83`.
* Golden `.ngc` files for small cases: facing, pocket, circle, hole grid; compare lines and coordinates.
* Validation: each rule gives `field + reason`; tool larger than pocket, depth below bottom, empty program,
  negative step, operation outside the stock.
* `parseWords` round-trip: generated G-code is parsed by the panel parser, modal codes agree. This is a syntax
  check, not geometry: arcs and cycles are also checked in the viewer.
* Preview is built from `CamPath` and matches `emit` on control cases.
* The generated program loads in the viewer: `--smoke`, `loaded OK`, no `[QML warning]`, segments and `UI_*`
  read.
* `cam.ts`: `isBusy` blocks, `CAM_CANCEL` is clean, writing only through `CAM_WRITE`, an existing file needs
  confirmation.
* Three configurations: P1 without touch, P2 keys and touch, P3 touch only.
* Operator acceptance: pocket alone, then three operations in one program, save, open, check, run on a demo
  part.

## 11. Risks and open questions

| Risk | What we do |
|---|---|
| Contour and radius compensation cause a gouge | MVP uses centreline only; `G41/G42` is a separate stage |
| The viewer does not understand some cycles | MVP uses `G81/G83`; check arcs; `G84` only later |
| Too many fields on 1024x600 | tabs, help, large targets; test on P1/P2/P3 |
| Model and `.ngc` diverge after manual edit | `.ngc` is marked generated; manual edit shows a warning |
| LinuxCNC is GPL | do not copy code; own algorithms, only interface ideas |
| Cutting data guessed by eye | take from material/tool, show the source, allow edits |
| An implicit operation produces a surprise | explicit toolpath, parameter summary and warnings; `Custom G Code` separate |
| One operation is not enough for a part | Q1 is only the vertical slice; Q2 must add the list and three operations in one program |
| `parseWords` does not check geometry | geometry tests + `gcodeviewer --smoke`/offscreen; round-trip is not proof |
| Generation freezes the GUI | 150-200 ms debounce, ~100 ms budget, then a worker thread |
| Writing overwrites someone else's program | `CAM_WRITE` only with a name, overwrite confirmation and backup/cancel |
| `G41/G42` undecided | MVP uses explicit centreline and `G40`; compensation is a separate stage |
| Collision is named but not defined | MVP uses the existing viewer check; 2D preview reports `UNVERIFIED`, not "clear" |

Open questions: exact MVP operation list; inch support; contour now or later; `.qprog` format and storage;
O-subroutines later; who provides the material table and cutting data; DXF in the near version. Stage versions
are not fixed to `0.19`-`0.24`: they follow M5 and the current release, otherwise the plan becomes stale after
one commit.

## 12. Sources

* LinuxCNC, NGCGUI: <https://linuxcnc.org/docs/html/gui/ngcgui.html>
* LinuxCNC, QtPlasmaC Conversational Shape Library: <https://linuxcnc.org/docs/html/plasma/qtplasmac.html>
* Fanuc Manual Guide i: <https://www.fanucamerica.com/products/software/cnc/manual-guide-i>
* Siemens SINUMERIK Operate, ShopMill/ShopTurn: <https://static.dc.siemens.com/cnc4you/CNC-Downloads/SINUMERIK-Operate-UserGuide-2013-09-bw-en.pdf>
* Heidenhain TNC7: <https://www.heidenhain.com/fileadmin/pdf/en/01_Products/Prospekte/PR_TNC7_Users_ID1384156_en.pdf>
* Haas Mill Programming Workbook: <https://haascnc.com/content/dam/haascnc/en/service/reference/programming-workbooks/mill---programming-workbook.pdf>
* Hurco WinMax Part Programming: <https://support.hurco.com/wp-content/uploads/hurco_mill/Conversational_Programming/Part_Programming.htm>
* Mazak MAZATROL: <https://www.mazak.com/us-en/news-media/useful-information/blog/G-Code-and-Conversational---MAZATROL-Does-it-All/>
* Okuma Advanced One Touch: <https://www.okuma.com/blog/advanced-one-touch-fully-optimize-your-multitasking-machine>
* Tormach PathPilot: <https://tormach.com/pathpilot-cnc-controller>
* NativeCAM, original project (GPL-2.0): <https://github.com/FernV/NativeCAM>
* NativeCAM 2.0b, Python 3/GTK3 port: <https://github.com/cnc-proton/nativecam-py3-gtk3>
* NativeCAM forum, including the implicit-operation warning: <https://forum.linuxcnc.org/nativecam>
