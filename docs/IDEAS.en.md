# IDEAS.md — development ideas (feature backlog)

A single backlog of feature ideas: a design with an estimate, not what is done. Part of the ideas used to live
in `TODO.md`; now they are all here, and `TODO.md` holds only technical debt. The source of truth about behaviour
is the code and its run (`CLAUDE.md`); the estimates here are a guide and are refined when an idea is taken up.
Russian copy — [IDEAS.md](IDEAS.md); both are one source, edited in one pass.

Sources of ideas: 1) the operator's list (sections 1-5); 2) a review of the external project ncSender (section
6); 3) an overview of related projects and ideas from there (sections 7-8). We take the idea and write the
implementation ourselves to our standards — the licences of the external projects are reviewed in section 7.

The document is kept as one: no separate file per idea or foundation, so the backlog does not fragment. The
readiness summary is section 9, the common foundations and the order of work are section 10.

## 0. How to read the estimates

* **S** — days, fits the current layers without new dependencies.
* **M** — weeks, needs a new module or a pass over several layers.
* **L** — a large work; the dependencies are named, do not start without them.
* **The "link to the controller" gate** — the panel has no real exchange with the controller yet: the machine
  state is local (`ARCHITECTURE.md`, section 2). Everything that needs telemetry or sending the NC program out
  first needs a link layer (protocol + transport), which the repository does not have.
* **The "hardware" gate** — a physical sensor, camera or external configurator is needed, which the project does
  not have.

Where things go (the map is `ARCHITECTURE.md`):

* **Viewer (`modules/gcode-viewer`)** — 3D scene, NC parser, geometry, OCCT/STEP, holder and collisions, removal
  field, 1:1 playback. Here too is the 3D interaction (picking).
* **Panel (`apps/react-interface`)** — pure logic in `lib/cnc/` (reducer, probing, offsets, alarms, service
  parameters) and the presentation layers.
* **Qt port (`apps/qt-interface`)** — repeats the panel verbatim.

## 1. Interactive 3D interface and G-code control

### Interactive 3D probing (Click-to-Probe)
**Idea.** A click on the B-Rep part in the scene -> the controller generates a measuring macro for the chosen
feature: a click into a cylinder — a hole-centre finding cycle; a click on two parallel faces — a width
measurement.

**Where we are.** The viewer already holds the STEP model and its faces (`cpp/occt/`, `cpp/core`), the panel has
the probing cycle model (`lib/cnc/probe.ts`), but there is no NC generator for probing (see section 6). A face
pick in QML/Quick3D is needed, feature classification (a cylinder, a pair of parallel planes, an end face) and a
cycle generator per feature.

**What is needed.** 1) Picking + feature type from the OCCT geometry; 2) a measuring code generator (two-stage
touch, zeroing); 3) the path "feature -> cycle -> macro". **Estimate: L**, depends on the probing generator
(section 6).

### Visual diff of NC versions
**Idea.** When a new NC program is loaded, the old toolpath is grey, the added/changed passes are bright.
An instant check of a technologist's edits.

**Where we are.** The viewer already parses the NC program into segments (`cpp/core/gcode_parser`, `path_prep`)
and draws them with the `toolpath.*` shader; the segments are addressable. The panel serves the files
(`lib/cnc/files.ts`).

**What is needed.** Layering of two parsed programs (matching by geometry/lines), a colour layer "matched /
added / removed", a switch. **Estimate: M** — a good candidate, it fits the existing parser.

### Selective machining (3D Window Machining / Scrap Recovery)
**Idea.** Selecting an undercut or a problem step with a 3D box right in the scene; the backend cuts everything
extra out of the NC program and generates a micro-program to finish only that piece.

**Where we are.** The viewer knows the geometry and the removal field (`stock_sim`) — it shows where material is
left; selecting a volume in the scene is a new layer.

**What is needed.** 1) Selecting a bbox/volume in the scene; 2) choosing the segments that fall into the volume;
3) recomposing the program with safe entries and leads (the chosen moves cannot simply be glued together). Item
3 is the most dangerous place: without correct entries/exits the micro-program is unsafe. **Estimate: L**; the
"link to the controller" gate for sending the result.

### Interactive editing of retracts (Drag-and-Drop Retracts)
**Idea.** Dragging a rapid (G0) line up in the scene with the mouse/finger; the controller finds the line in the
NC program and edits the safety height (Z clearance).

**Where we are.** Two things are needed that are not there together yet: picking a line in the scene and mapping
a segment back to an NC line number. The parser gives the segments; the "segment -> line" link has to be built.

**What is needed.** 1) A segment carries the source line number; 2) picking a G0 segment; 3) editing the
coordinate and writing the file. **Estimate: M**.

### Visual G68 rotation
**Idea.** On a program coordinate rotation (G68) the scene physically rotates the part and the toolpath relative
to the machine axes — the real picture on the table.

**Where we are.** The viewer holds the coordinate system and the WCS; parsing G68/G69 in the parser state is new.

**What is needed.** 1) Parsing and storing G68 (angle, centre) in the parser state; 2) rotating the part and
toolpath layers in the scene; 3) a separate display of the machine axes. **Estimate: M**.

### Coolant aim indicator
**Idea.** A semi-transparent cone of the coolant jet, so the operator sees the contact spot of the fluid with the
cutter before the start.

**Where we are.** The Quick3D scene and the nozzle material are a new object; the project has no nozzle geometry
data.
**What is needed.** 1) A nozzle and jet-cone model; 2) binding to the cutter position; 3) a transparent material.
**Estimate: M** (visualisation); without hardware data it stays a simulation.

## 2. Predictive analytics and vibration monitoring

The block's common dependency is the **"link to the controller" gate + the "hardware" gate** (a vibration sensor
over RS-485/MEMS). While they are absent, only visual data sinks (colouring, charts) on a demo stream are
feasible.

### Load heatmap of the toolpath
**Idea.** Colouring the toolpath line with a gradient in real time by the RMS/Kurtosis of the vibration sensor —
the places of overload and resonance are visible.

**Where we are.** The viewer can colour segments (an example is the feed/height colouring in Candle, section 8);
there is no vibration data channel.

**What is needed.** 1) Receiving telemetry; 2) binding a sample to a segment/time; 3) gradient colouring.
**Estimate: M** for the visualisation, L including the data collection.

### Auto-feed tuning
**Idea.** Recording the load of the first pass; suggesting to split the NC program, raise the feed on the "air"
sections and lower it before heavy plunges.

**Where we are.** Analytics + rewriting F in the NC program (an example of edits is `gcode-transformer`, section
6).
**What is needed.** 1) Collecting the load; 2) finding the "air" and heavy sections; 3) editing the feeds.
**Estimate: L**; the "link to the controller" gate.

### Cutting "golden standard" (Acoustic Fingerprint)
**Idea.** Recording the frequency profile of a reference part; on the next parts the controller compares the
spectrum and signals/stops the machine (the cutter is blunt, the stock is harder).

**Where we are.** There is no spectral analysis; this is a separate DSP task.
**What is needed.** 1) Capturing and FFT of the signal; 2) a reference profile; 3) a divergence metric and a
threshold.
**Estimate: L**, far away; the "hardware" gate.

### Volumetric wear tracking
**Idea.** Tool life not by minutes but by the material actually removed (MRR, mm3), computed through a Z-buffer or
OCCT booleans.

**Where we are.** The most realistic item of the block: the viewer already builds the removal field
(`cpp/core/stock_sim`) — the removed volume is integrated over the field; OCCT (`cpp/occt/`) is available for the
boolean variant.

**What is needed.** 1) The integral of the removed volume over the height field; 2) binding to the tool; 3)
accounting per part.
**Estimate: M**, it fits the existing layer.

## 3. Advanced simulation and safety (Look-ahead)

### Ghost look-ahead projection
**Idea.** A semi-transparent cutter runs along the toolpath 3-5 seconds ahead of the real tool.

**Where we are.** The viewer already plays the program 1:1 with the feed — there is a tool marker. A second
marker with a time/segment lead is needed.

**What is needed.** 1) A second marker with a time offset; 2) a semi-transparent material; 3) binding to the same
player. **Estimate: S** — small, it fits the playback directly.

### Safety tube (swept volume collision)
**Idea.** Sweeping a wide-holder profile along the G0 lines; OCCT checks the extruded volume for intersection
with the part/vise and highlights the dangerous manoeuvres.

**Where we are.** The viewer already computes the "holder vs stock" collision (`cpp/core/collision`, `tooling`)
and draws the ER20 holder. Here it is the same apparatus, but for rapid moves and volume.

**What is needed.** 1) A swept volume of the holder profile along the G0 toolpath; 2) intersection with the part
and the fixture (OCCT); 3) highlighting the dangerous sections. **Estimate: M** — there is a solid basis.

### Kinematically exact time predictor
**Idea.** A background run of the NC program through the real planner (accelerations/jerk) for a "to the second"
time, recomputed when the feed override turns.

**Where we are.** There are "1:1 speeds" and a tool-based time estimate (`lib/cnc/tool-time.ts`), but without an
acceleration/deceleration planner.
**What is needed.** 1) A planner (accelerations, jerk) in pure C++ (`cpp/core`) as a separate unit; 2) taking the
override into account; 3) recomputing on the fly. **Estimate: L** — a large but isolatable unit.

## 4. Fixture and CMM management (Measurement)

### Virtual fixture snapping
**Idea.** Loading STEP models of the fixture; the probe touches the physical vise — their 3D model in the
interface "snaps" to the right coordinates for an exact collision computation.

**Where we are.** STEP import exists (`cpp/occt/`), collisions exist (`cpp/core/collision`) — what is missing is
the fixture as a scene object and snapping by touches.
**What is needed.** 1) Importing and storing fixture models; 2) registering touches with the probe; 3) aligning
the model.
**Estimate: L**; partly the "link to the controller" gate (real touches).

### Interactive deviation map (On-Machine Inspection Report)
**Idea.** After measuring the part with the probe, the touch points are projected onto the STEP model faces and
coloured: green — within tolerance, yellow — undercut, red — overcut. The machine as an optical CMM.

**Where we are.** STEP and OCCT are in place; there are no measuring points and tolerances.
**What is needed.** 1) Storing measuring points and nominals; 2) projecting a point onto a face, computing the
deviation; 3) colouring by tolerance and a report. **Estimate: L**; the "link to the controller" gate.

### Seamless tool import (Zero-Data-Entry)
**Idea.** Entering tool data (length, radius, profile type) by scanning a QR on the holder; the controller loads
the 3D profile into ToolGeometry itself — no typos in the zeros.

**Where we are.** There is a tool check (`lib/cnc/tool-check.ts`) and a tool table; there is no camera.
**What is needed.** 1) A QR scanner (device/stream); 2) a tool registry with profiles; 3) loading the parameters.
**Estimate: M** for the UI, the "hardware" gate for the scanner.

## 5. Protection from the "human factor" and convenience

### "Time machine" and safe restart (Smart Block Search)
**Idea.** When the cutter breaks, the operator picks a point on the 3D toolpath; the controller generates the safe
code to return to work (spindle start, Z approaches with clearance).

**Where we are.** Directly tied to the parser and the safe-continuation generator (section 6). What is needed is
mapping a scene point to an NC line plus the generator itself.

**What is needed.** 1) Analysing the state up to the line; 2) a preamble generator; 3) picking a point on the
toolpath.
**Estimate: M-L** — a superstructure over the continuation generator.

### Ctrl+Z for offsets
**Idea.** A history of changes to the part-zero tables (G54) and tool lengths, protection from accidentally
zeroing the wrong axis.

**Where we are.** The item closest to ready: probing already stores the previous value (`prev` in
`lib/cnc/machine.ts`) — the same technique extends to offsets and tools.

**What is needed.** 1) A history stack of offset/tool changes; 2) Undo/Redo through an action; 3) showing what is
being rolled back. **Estimate: S-M** — it fits the current reducer.

### Semantic alarm recovery wizard
**Idea.** On an error (for example, a Following Error inside metal) the interface walks the operator step by step,
with 3D hints, through a safe tool extraction, blocking dangerous presses.

**Where we are.** There are alarms (`lib/cnc/messages.ts`), input checks (`guards.ts`) and screen locking by mode
(`MODE_LOCKED_SCREENS`). Added here is a "code -> recovery scenario" table (an example of alarm interpretation is
section 6).

**What is needed.** 1) Scenarios by alarm codes; 2) a step-by-step screen that blocks dangerous actions; 3) a 3D
extraction hint. **Estimate: M**.

### Ballscrew mapping wizard
**Idea.** Interactive creation of `.comp` files for screw-straightness compensation from indicator readings,
without typing text tables by hand.

**Where we are.** There is none; it is tied to the `.comp` format (LinuxCNC).
**What is needed.** 1) Entering measurements by points; 2) building the correction table; 3) writing the `.comp`.
**Estimate: M-L**; depends on the target environment (LinuxCNC).

### Doing away with configurators
**Idea.** Direct control of `.ini` and `.hal` from the service tab: the backend rewrites the configs and reloads
HAL itself.

**Where we are.** There is a service parameter table through `ParamDef` (`lib/cnc/service-data.ts`), but it is a
local simulator model — there is no writing of real configs.
**What is needed.** 1) Real integration with the machine config; 2) safe writing of `.ini`/`.hal`; 3) reloading
HAL. **Estimate: L**, far away; the "link to the controller" gate and the target environment.

## 6. Ideas from the external project ncSender

A review of `github.com/siganberg/ncSender` (a CNC controller: C# server + Vue + Electron). From there we take
domain algorithms and UX patterns, and rewrite the code ourselves to our standards (the core — without Qt/DOM;
`CLAUDE.md`).

* **Generating probing G-code.** Our `lib/cnc/probe.ts`/`probe-exec.ts` compute the cycle result but do not emit
  an NC program. Add a generator: a two-stage touch (`G38.2 F150` -> bounce -> `G38.2 F75`), `G4 P0.3`, zeroing
  `G10 L20`, probe radius compensation, corners/centres (inner/outer), a variable for returning the units. An
  example — `Probing/ProbeCommandGenerator.cs`, `Strategies/*.cs`. Observe the FluidNC remark: `G0 X-[expr]` is
  not allowed, change the operands.
* **Safe restart "from a given line".** Analysing the NC program up to line N and restoring the modal state
  (G20/21, G17-19, G54-59, G90/91, M3/4+S, M7/8, M64/65, tool, F), then a continuation preamble: retract
  `G53 G0 Z`, tool change, approach to height, plunge, return of the modes and F; separate arc logic (finding the
  start of `G2/G3`). A guide — `GcodeAnalysis/GcodeStateAnalyzer.cs` + `ResumeSequenceGenerator.cs`. The place is
  the viewer's pure core `cpp/core/` (the arc parser is already there).
* **An alarm table "code -> title + how to fix".** Our `messages.ts`/`types.Alarm` store codes and levels, but
  without an explanation. Take the idea of `lib/alarm-help.ts`: a human title and a treatment line for each code +
  a fallback for "an alarm without a code" (parsing a `Pn` pin line, recognising a power-on E-stop check).
* **A machine profile registry.** A declarative registry (brand/model/size/board) with ready sets of firmware
  parameters (`$100-$132`, `$22`, `$27`, `$745`) that pre-fills the table. A guide —
  `MachineProfiles/machine-profiles.json`. Our parameters are described one by one (`service-data.ts`,
  `ParamDef`); a profile is a layer on top. The data logically belongs next to `cad-testdata`.
* **NC transformations: rotate/mirror/shift.** Take the approach of
  `features/toolpath/transform/gcode-transformer.ts`: correct arcs (I/J/K and the R format, short/long by the sign
  of R), rotating the arc plane G17<->G18<->G19 and reversing G2/G3 by the viewing axis, preserving the source
  decimal precision of the file. The place is `cpp/core/`.
* **A command registry with an availability predicate.** The pattern of `lib/command-registry.ts`:
  `{id,label,group,handler,isEnabled,requiresLongPress}` and a single `execute()`. It lets softkeys, the physical
  console and gestures call one action; `isEnabled` and long-press are protection for "Stop"/E-stop. Apply to
  `softkeys.ts` and `components/cnc/hardware`.
* **Check the job stream for lost lines on Hold.** In `Job/GcodeJobProcessor.cs` a real defect is analysed: the
  guard reads the last status report with a delay, which made `M0` in a tool change "eat" the positioning and F,
  and the machine went into an arc without a start. Compare with our simulator (`lib/cnc/prog.ts`).
* **Keep in mind (lower priority):** tool length measurement and the carousel (`Job/HomeToolMeasure.cs`,
  `features/tools`; next to our `tool-check.ts`); parameter backup/restore (`features/backup`); UI tips
  (`Tips/BuiltInTips.cs`); automatic unit conversion (`lib/units.ts`); JS plugins as a command extension.

## 7. Related projects and licences

Senders/controllers of the same class as ncSender. The data was checked through the GitHub API (October 2026).

**The licence rule:** permissive (MIT/BSD) — the code may be reused with attribution; GPL — only the idea, we
rewrite it ourselves. ncSender is GPL-3.0, our project is Apache-2.0: directly porting files is not allowed.

| Project | Stack | Licence | ★ | What it is useful for |
|---|---|---|---|---|
| **CNCjs** | Node.js / JS (web) | **MIT** | 2658 | A widget system, plugins, pendant, a watch folder. The code may be reused. |
| **Universal Gcode Sender** | Java | GPL-3.0 | 2272 | A classic, UGSPlatform, streaming algorithms. |
| **Candle** | **C++ / Qt** | GPL-3.0 | 1703 | Matches our stack: a 3D NC visualiser, a console, plugins, scripts. |
| **bCNC** | Python | GPL-2.0 | 1750 | Auto-levelling, CAM plugins, an editor. (Inside — subcomponents under BSD3/MIT.) |
| **ESP3D-WEBUI** | Preact / web | GPL-3.0 | 899 | An embedded ESP32 web UI, a multi-target build, extensions/themes. |
| **gSender** | TypeScript / Electron | GPL-3.0 | 382 | A modern UX reference of the grblHAL panel (a CNCjs fork). |
| **ioSender** | C# / .NET | **BSD-3-Clause** | 288 | A grblHAL reference: a probing set, dynamic settings, lathe. The code may be reused. |
| **OpenBuilds CONTROL** | JS / Electron | GPL-3.0 | 269 | Another Electron sender; profiles, probing. |
| **AxioCNC** | web | MIT | 29 | A young web controller (Grbl/Marlin/Smoothie/TinyG). |

Firmware and the ecosystem (to understand the protocol, not for code): `grblHAL/core` (C), `gnea/grbl` (C),
`FluidNC` (C++), plus the 3D-printing `Klipper`/`Marlin` and their web UIs (Mainsail/Fluidd).

## 8. Ideas from related projects

The features were reviewed in the sources; below — the idea -> where it fits with us. GPL projects give only the
idea.

* **Candle — the NC drawing architecture (`drawers/`).** A base `ShaderDrawable` class and a set of drawers:
  `gcodedrawer` (**Vectors/Raster** modes, colouring by state — normal/drawn/highlight/Z-move/start/end,
  **shades by S or Z**, `simplify` with a tolerance), `heightmapgrid/border/interpolation` (the field grid),
  `tooldrawer`, `machinebounds`, `origin`, `selection`. The idea is good for our `gcode-viewer` (compare with our
  palette and simplification). Candle plugins: a camera, coordinate systems, E-stop, user commands + a built-in
  **scripting engine**.
* **CNCjs (MIT) — the widget system and streaming.** Each widget is an independent module (`Probe`, `Autolevel`,
  `Visualizer`, `Macro`, `Custom` — a widget loaded **from a URL**, `Webcam`). The server supports the
  **character-counting** and **send-response** protocols. The idea: reduce our panels/screens to widgets with a
  single registration contract; a `Custom` widget from a URL — as an extension without touching the core.
* **ioSender (BSD-3) — a full probing set and dynamic settings.** `CNC Controls Probing`: edge finder
  (inner/outer), centre finder (inner/outer), rotation, tool length, height map, `ProbeVerify`, `ProbingMacros`,
  `GCodeTransform` (height-map overlay, rotation). Separately valuable is the **Grbl Config App**: the settings UI
  is **generated from data and controller responses, with help on the screen** — our `service-data.ts`/`ParamDef`
  ask for exactly such a panel generator with explanations.
* **bCNC — editor and CAM ideas (to rewrite).** Moving/rotating/mirroring the NC program, **optimising rapid
  moves**, computing G-code functions with expansion during sending, expanding canned cycles, auto-levelling "on
  the fly" during sending, user buttons, a web pendant, a material and tool database, dozens of CAM plugins
  (`box`, `bowl`, `driller`, `heightmap`, `stlSlicer`, `dragknife`).
* **ESP3D-WEBUI — multi-target and extensions.** Building one codebase for `CNC`/`Printer3D`/`SandTable` through
  `TARGET_ENV`/`SUBTARGET_ENV`; the `areas/tabs/targets/features/interface/machine` structure; the
  `extensions_samples` (an extension example) and `themes_samples` directories — **plugins and themes** as data.
  The idea: panel extensibility without forking the core; useful for `react-interface`/`qt-interface`.
* **Common — i18n.** ESP3D has separate language packs with translation completeness checks; compare with our
  ru/en softkey and message strings (a single dictionary, so the translation does not diverge).

## 9. Readiness summary

| Priority | Idea | Estimate | Basis in the project |
|---|---|---|---|
| Near | Ghost Look-ahead | S | 1:1 playback |
| Near | Ctrl+Z for offsets | S-M | `prev` in the reducer |
| Near | Diff of NC versions | M | the viewer's segment parser |
| Near | Volumetric Wear Tracking | M | the `stock_sim` removal field |
| Near | Swept Volume Collision | M | the "holder vs stock" collision |
| Near | Visual G68 | M | the parser state + the scene |
| Near | Recovery Wizard | M | alarms, guards, screen locking |
| Medium | Drag-and-Drop Retracts | M | the parser + picking (the segment-line link) |
| Medium | Click-to-Probe | L | OCCT + the probing generator |
| Medium | Smart Block Search | M-L | the safe-continuation generator |
| Medium | Load Heatmap / Auto-Feed | M / L | segment colouring; the "link" gate |
| Far | 3D Window Machining | L | geometry + NC recomposition |
| Far | Fixture Snapping / Inspection Report | L | OCCT/STEP; the "link" gate |
| Far | Acoustic Fingerprint | L | none; the "hardware" gate |
| Far | Screw Mapping / .ini-.hal | L | none; the target environment |
| Far | Coolant aim, QR tool | M | visualisation/UI; the "hardware" gate |

## 10. Foundations: common grounds and the order of work

The ideas are not independent: several rest on one foundation. Below — a foundation, what it gives and how many
ideas stand behind it; the order is set so that one investment brings several ideas to life at once.

| Foundation | What it gives | Unblocks the ideas | Estimate | First step |
|---|---|---|---|---|
| Link to the controller (protocol + transport) | real telemetry and sending the NC program | Load Heatmap, Auto-Feed, Acoustic Fingerprint; Fixture Snapping, Inspection Report, .ini/.hal | L (the gate of all of section 2) | a transport module in `lib/cnc` + an echo stub under test |
| 3D picking (faces, lines, points) | choosing an object in the viewer scene | Click-to-Probe, Drag-and-Drop Retracts, Smart Block Search, Fixture Snapping | M | `pick` from `GcodeViewer3D.qml` into the C++ controller, printing the object id |
| The "segment -> NC line" link in the parser | knowing where a move came from | Diff of versions, Drag-and-Drop Retracts, Smart Block Search, 3D Window Machining | M | a `line` field in the segment + a matching probe |
| Probing NC generator | emitting measuring cycles (section 6) | Click-to-Probe, Fixture Snapping, part of Inspection Report | L | a Z-cycle generator (two-stage touch) in `lib/cnc` + a test |
| Safe-continuation generator | state analysis + a preamble (section 6) | Smart Block Search, Recovery Wizard | M-L | a state analyser up to the line + a probe on `Test_model.ngc` |
| Toolpath planner (accelerations, jerk) | honest time and a time lead | Time predictor, Ghost Look-ahead | L | the time by G0/G1 with axis acceleration in `cpp/core` + a probe |
| Removal field / volumetric model (`stock_sim`) | knowledge of the removed material | Volumetric Wear Tracking, 3D Window Machining | M | the removed-volume function over the height field + a probe |
| Collision apparatus (holder, stock) | intersection checks | Swept Volume Collision, Fixture Snapping | M | a swept volume of the holder along one G0 segment + a probe |

The order (by return on the investment):

1. **Link to the controller** — the only foundation that brings all of section 2 and part of 4-5 to life; without
   it they stay a demonstration on a local model.
2. **3D picking + the "segment -> line" link** — together they open four interaction ideas (Click-to-Probe,
   Retracts, Block Search, Diff).
3. **Probing and continuation generators** (section 6) — the common basis of Click-to-Probe and Smart Block
   Search.
4. **Toolpath planner** — a self-contained large unit; it brings the time predictor to life and makes Ghost
   Look-ahead honest (a lead by time, not by segments).
5. **Removal field and collisions** — already there; extend to volume (Wear Tracking) and swept volume.
