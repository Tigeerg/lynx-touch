# DESIGN.md — 3D viewer for NC programs (`gcode-viewer`)

The source of truth is the code and its behavior (CLAUDE.md), not this description: the document explains the
design and may lag behind. Change this file together with behavior. Russian version — [DESIGN.md](DESIGN.md);
edit both languages in the same pass.

## 1. Purpose and place in the product

`gcode-viewer` is the third of the product's four projects (manifest — [projects.json](projects.json)):
interactive 3D preview of a machine control program. Directory — `modules/gcode-viewer`. Stack: QML + QtQuick3D +
PySide6; the heavy part is Python.

Dependencies:
- `numpy` — required on the controller (without it geometry and outlines are computed much slower; the code
  prints a warning on import);
- `cadquery-ocp` (OpenCascade, OCP) — needed only to read STEP; without it the part is built from STL.

The viewer's contract with the post processor is the `UI_*` tags in NC comments (`UI_STOCK`, `UI_TOOL`,
`UI_OP`, `UI_HOLDER`, `UI_ROTATION`, `UI_ORIGIN`, `UI_CAD_MATRIX`). The tags come from `cad-testdata`
(`data/cad-testdata/linuxcnc_ui.cps`); when a tag changes, edit the post and the parser in the same pass (VERSIONING.md,
section 7).

## 2. Entry points

- `build/viewer/gcodeviewer` (`cpp/viewer/main.cpp`) — the canonical viewer launch, no Python: a `QQuickView`
  over `GcodeViewer3D.qml`, controller QML module `MockBackend` from `build/qmlbackend`. Build — README,
  "Building and running the viewer (C++)".
- `preview_3d.py` — a checks tool: the same QML loaded through PySide6 (both Python and the built
  `MockBackend` plugin are required); in offscreen mode it takes the synchronous path and prints `loaded OK`.
  `smoke_test_qml.py` and the `pnpm guard` watcher run through it.
- `preview_3d.qml` — a 1024x600 `Window` holding `GcodeViewer3D`.
- `preview_qml.py` — a launch point of another project (the Qt interface, `apps/qt-interface`); it has nothing
  to do with the viewer.

Headless run: set `QT_QPA_PLATFORM=offscreen`.

## 3. Architecture: layers

```
Python: parser and preparation (pure, no Qt)
        G-code parsing, decimation, buffer packing
   |  signals
Python: controller GcodeToolpath (QObject) and QQuick3DGeometry shapes
   |  QML properties/slots
QML:    GcodeViewer3D.qml — scene, camera, materials, buttons, animation
   |
GPU:    QtQuick3D draws triangles and lines
```

- **Pure core.** `parse_gcode` and `_prepare_ordered` do not touch Qt and are thread-safe: they can run in any
  thread. This is what lets the load leave the GUI thread.
- **Geometries.** Each is a `QQuick3DGeometry` subclass; Python supplies the binary vertex buffer, primitive
  type, attributes and bounds, and QtQuick3D draws it (one shared pattern: `clear -> setVertexData ->
  setStride -> addAttribute -> setBounds -> update`).
- **Controller.** `GcodeToolpath` owns the geometries and the properties QML sees (`center`, `radius`,
  `moveLength`, `stages`, `collisions`) and the slots (`sampleAt`, `speedAt`, `isRapidAt`, `refreshTangents`,
  `setToolStage`, `load_gcode_async`).
- **QML.** Describes the scene and the interaction; machine state lives in Python.

## 4. Data model and the QML contract

The controller exposes finished data; QML never receives the per-point path lists:
- `sampleAt(d)` — interpolated tool position at path distance `d` (binary search over `cum` plus linear
  interpolation);
- `speedAt(d)` / `isRapidAt(d)` — speed and rapid flag of the segment;
- `stages` — program operations as normalized `[start, end]` path ranges;
- `collisions` — collision ranges with a representative scene point.

A group of values that travels together is a named type (CLAUDE.md): the module-boundary structures live in
`gv_types.py` (`PathPoint`, `Stage`, `Aabb`, `PackedEdges`, `PathData`, `CylinderSurface`/`ConeSurface`,
`TangentFamilies`, `MeshTopology`, `ProfilePoint`/`HolderSection`). A dict remains only where QML reads it
(`toolInfo`, `stages`, `collisions`, `toolCheck`): that is the QML contract, not debt.

## 5. Coordinate system

The machine works in (X, Y, Z) with Z up; the scene uses Y up. One permutation covers the whole code
(`_to_3d`): machine `(X, Y, Z) -> scene (X, Z, -Y)`. Units are millimeters, the scene is 1:1 with the part, so
"scene units == machine mm". Every machine <-> scene conversion goes through this function.

## 6. Threads and asynchrony

Loading (`load_gcode_async`) starts a background thread `_worker_load`:
1. check the disk cache, otherwise parse the program and prepare the buffers, then write the cache;
2. signal `toolpathReady` -> the GUI applies the toolpath;
3. read the model (STEP/STL) in the background;
4. signal `meshReady` -> the GUI applies the part.

Each result carries a token (`_load_token`, `_edge_token`, `_collision_token`): a stale result (the user opened
another program) is dropped. The STL outline and the collision scan run in their own threads.

Two correctness mechanisms:
- **`_occt`** — a global `RLock` around all OpenCascade calls. OCCT releases the GIL inside heavy calls, and
  two threads on one `TopoDS_Shape` corrupt memory; the sections are serialized.
- **CppOwnership** — `QQmlEngine.setObjectOwnership(...)` tells Qt that Python owns the object; without it the
  QML engine could delete a geometry object.

## 7. Disk cache

`_cache_key` is a sha1 over the file path, size, modification time, the format version `CACHE_VER` and the
decimation tolerances. The key selects an `.npz` file (no pickle) in `~/.cache/gcode_viewer`. Kinds: path,
STL outline, STEP model. Writes are atomic (`os.replace`); the directory is trimmed to `CACHE_MAX_BYTES`. On
read the arrays are copied (`.copy()`): otherwise numpy keeps a buffer of a closed archive — a use-after-free.
`CNC_NO_CACHE=1` disables the cache.

## 8. Part model: STEP and STL

Next to the program, a `.step`/`.stp` is looked up, otherwise a `.stl`.

**STEP (primary path).** The B-rep is read once (`_step_shape`) and used for both the skin and the analytic
data. `load_step_model` tessellates the surfaces (deflection 0.05 mm) and extracts the true B-rep edges
(cylinder seams are filtered out as form lines). `_smooth_face_normals` builds crease-aware normals.
`load_step_surfaces` separately collects analytic cylinders and cones (axis, radius, arc) — the silhouette
needs them.

**STL (fallback).** A triangle soup without topology; the outline is a dihedral-angle heuristic
(`_feature_edges`, threshold 20 degrees). Its edges arrive as chains of sub-millimetre links (the median
edge of Test_model.STL is 0.07 mm), so the chains are welded and simplified (`_simplify_edge_chains`,
tolerance 0.15 mm): a ribbon is built from the two ends of a segment, and on a sub-pixel link its direction
is noise. **Limitation:** the heuristic only sees sharp transitions. B-rep face boundaries where the surfaces
meet tangentially (blend boundaries, seams) cannot be told from triangulation edges by angle, so the STL
outline carries fewer internal contours than the STEP one - a property of the fallback, not a defect. The
threshold was swept: 20 -> 2 degrees adds about 290 segments and does not reach the STEP network (6825 mm),
while below 1.5 degrees the outline explodes tenfold on curved-surface facets.

**Shell orientation.** The sign of a normal sets the shading of a face, so the shell is turned outward once
per model - `_orient_shell_outward` (for a closed solid the area-weighted normals point away from the
centroid). A per-face flip about the centroid is not applied: on concave regions it turns the normals of
pockets and holes inward. Guarded by `test_normals_orientation.py`.

## 9. Outline and silhouette

The classic CAD look is a shaded part plus two kinds of contours:
- **static B-rep edges** — real face boundaries, independent of the view angle;
- **dynamic silhouette (tangents)** — tangent generatrices of holes and cylinders, dependent on the camera
  eye.

Hidden lines are removed by the GPU depth test: the skin is opaque and writes depth, and the contours render
with a small shift toward the camera. That gives hidden line removal for free, with no CPU visibility pass.

**Ribbons instead of GL_LINES.** Real GL_LINES are always 1 px and antialias poorly; each edge is drawn as two
triangles (`HLRLineGeometry`) whose width the vertex shader keeps at a constant number of pixels.

**Tangent families.** `_tangent_families` is precomputed once per load: cylindrical faces are grouped by axis
and radius; arc coverage is measured on the skin mesh (`ARC`) and generator lines that coincide with a static
edge are collected (`DROP`). `_tangent_from_families` computes the tangents on every camera move (fast, under a
millisecond), dropping duplicates of the static edges. This complexity is justified: the naive silhouette
produced "double lines that drift off the contour".

## 10. Tool and holder

The tool is built procedurally from `UI_TOOL`: `_tool_profile` builds a 2D profile `(radius, height)` for the
cutter type (flat, ball, bull, chamfer/taper, drill, ...), and `ToolGeometry` revolves it into a solid of
revolution. The outline is an inverted hull (an enlarged copy that renders only its far faces). The holder is a
coarse ER20 model (nut plus body) from `_holder_solid_profile`; the stick-out may come from `UI_HOLDER`. The
tool position is `sampleAt(pathDist)`.

## 11. Animation, collisions, tool check

- **Animation.** `FrameAnimation` fires once per frame and reports the real frame time, so playback speed does
  not depend on FPS. Speed is 1:1 with the machine: feed `F` (mm/min -> mm/s), rapids at a fixed rate.
- **Hiding the traveled path.** A custom material `toolpath.vert/frag` discards fragments whose path parameter
  is below the progress (`cutoff`); there is a fade of the far section and a stages mode.
- **Collisions.** `_scan_collisions`: a background check of the holder cylinders against the stock bounding
  box at a 1 mm step; the result becomes red markers in the scene.
- **Tool check.** `_tool_check_report` compares the program tool with the controller tool table (numbers,
  stick-out). It is a skeleton: the table is fed from outside (`setMachineTools`).

## 12. Known problems and technical debt

Architecture:
- `mock_backend.py` is a 107-line facade that re-exports the split modules (`gv_types`, `gcode_parser`,
  `step_import`, `tangents`, `collision`, `geometry`, `controller` and others); the monolith split is done
  (`PORT-CPP.md` §7) and the outside code and QML still see the old names.
- `GcodeViewer3D.qml` (about 1000 lines) holds JS camera, projection and stage logic; it belongs in backend
  slots.

Code against the CLAUDE.md rules:
- module-boundary structures are no longer tuples/dicts: they live in `gv_types.py` (0.13.13); a dict remains
  only where QML reads it (`toolInfo`, `stages`, `collisions`, `toolCheck`) — a contract, not debt;
- the display toggles (`showPartVisible`, `xray`, `showBrepEdges`, ...) are a run of booleans, not a mode;
- many magic numbers without a name or unit;
- development-history comments (the rules forbid them);
- mixed comment languages (Russian and English);
- no type annotations.

Checks:
- tests depend on private functions (fragile under refactoring);
- `refreshTangents` is wrapped in an outer `try/except Exception: pass` — an unrelated per-frame error
  stays invisible; the C++ path failures report themselves;
- the live frame checks (`test_solid_skin.py`, `test_ribbon_width.py`) need the reference stand: the
  measurements were taken on another machine and on the development machine they are red on a clean `HEAD`
  (11792 px against a threshold of 10500; 0.00 px against 2.0). The guard does run them as stand checks: they
  execute under `CNC_LIVE_FRAMES=1` (a live window, hardware GL), otherwise they are a `skip` with the reason.
  The thresholds are still tied to the stand — calibrating them for other GPUs remains debt.

Remaining after the C++ port (open, with measurements):
- the panel `apps/qt-interface/main.qml` (and the generated `preview.qml`) compares hotkeys by
  `nativeVirtualKey`, which the QML `KeyEvent` type does not expose at all (verified in the viewer: the
  property is `undefined`), so the panel hotkeys depend on the layout; fixing it needs regenerating
  `preview.qml` (`apps/qt-interface/gen_preview_qml.py`) and its own `qt-interface` version;
- the C++ entry point `gcodeviewer.exe` does not accept an absolute NC path with Cyrillic characters:
  the arguments are read as `QString::fromLocal8Bit(argv[i])`, so `GetCommandLineW`/`CommandLineToArgvW`
  is needed;
- the `.npz` disk cache was not ported to the C++ path and stays a development tool for the Python
  reference;
- the optimisations from `PORT-CPP.md` §11: the NC parse (176 ms), the STEP shape cache in the plugin,
  quality presets and level auto-detection, trimming the OCCT build, parallelism of the cold stages.

Outline on the STL branch:
- On a mesh only sharp transitions are drawn, so there are fewer internal contours than on STEP: B-rep face
  boundaries where the surfaces meet tangentially (blend boundaries, seams) cannot be told from triangulation
  edges by angle. Measured: the threshold 20 -> 2 degrees adds about 290 segments and does not reach the STEP
  network (6825 mm), while below 1.5 degrees the outline explodes tenfold (15092 segments). Completion:
  detect tangential boundaries by the curvature break between neighbouring faces (second derivative), with its
  own check on Test_model. STL is an emergency path, so the work has not been started.

Performance on weak hardware (Intel Atom + Nvidia Ion):
- `MSAA High` on the whole frame is the main cost; the ribbons already have distance-field AA and do not need
  MSAA;
- `xray: true` by default makes the skin transparent (full-screen overdraw);
- a PBR skin under three `DirectionalLight`s is heavy over a large area;
- the ION driver is the legacy OpenGL 3.3 branch, there is no Vulkan, and D3D11 may not come up; a hardware
  GL context is required, not a software rasterizer;
- the most expensive per-frame path is the silhouette: the topology and the tangent families travel as flat
  arrays and are copied again on every camera move (about 2.6 MB per frame at 45398 faces);
- there is no parallelism in the C++ (except `BRepMesh`): tessellation, soup assembly and the cone policy
  are single-threaded;
- the quality thresholds (tessellation deflection, edge angle, coalescing step, outline simplification) are
  hard-coded and do not depend on the hardware;
- the STEP soup and normals are materialised 6-7 times on the way to the buffers: about 20-25 MB of
  transient copies per Test_model load;
- `gvqtgeom` (the C++ Qt geometries) builds but is wired only to a probe — QML still runs the Python
  geometries; `Eigen` is not used in the C++ at all (bit-for-bit requires a hand-written summation order).

## 13. Directions for improvement

Functionality:
- material removal simulation (heightmap/dexel, in the background on a weak CPU): the height map as a dynamic
  2D texture of the Z-buffer that a vertex shader extrudes into plane polygons; the design, the cost and the
  checks are in "Material removal simulation" at the end of this section;
- gouge check of the tool against the part model and cutting-mode control: compare the Z depth of cut with
  the flute length and the tool diameter from a database (SQLite), so a smooth shank does not cut into the
  part;
- machining time and ETA (`moveLength`/`speeds` already exist);
- linking 3D and the NC line (click a path point -> block/line, search);
- section planes, measurements, point coordinates;
- work offsets G54..G59, reference point;
- STEP holder models (B-rep import already exists; needs an anchor/gauge, a model catalog, a cache, and
  collision primitives from the analytic cylinders instead of the hard-coded ER20);
- FPS meter, a dark theme, program drag-and-drop.

NC analysis and collision protection (look-ahead):
- a background pre-parse of the NC program: a C++ pass over the trajectory before the machine starts;
- rapid-move control: block the program if a `G0` runs through coordinates of the stock allowance not yet
  removed;
- depth-of-cut and flute-length control against the tool database — see above.

Machine control and safety:
- a dual PID loop: the inner loop in the servo drives (tuned over RS485/Modbus), the outer loop in LinuxCNC
  fed back by linear encoders;
- adaptive spindle control in HAL: real-time load monitoring; above the threshold a `wcomp` comparator puts
  the machine into an emergency stop (`halui.machine.off`), protecting the cutter from a jam.

Tool management and probing:
- logical decoupling of the CAM tool (`T`) from the automatic tool changer pocket (`P`);
- protection against a wrong magazine load (`M6` remap): after the change the machine goes to the probe and
  compares the measured length with the database reference; on a mismatch the program is blocked;
- dynamic calibration (`G38.2` + `M4`): length and diameter measured by a touch probe at low rpm in reverse
  rotation, accounting for the actual run-out of the cutting edges;
- polymorphic probing macros: the C++ backend picks the macro by cutter type (centre touch for solid
  cutters, a radius offset for shell cutters with an empty centre).

CAD-style visualisation (Qt Quick 3D + OCCT):
- done: exact B-rep edges from STEP rendered as `GL_LINES` (the `CNC.Edges` plugin, 0.14.0), normal smoothing
  by crease angle that keeps sharp corners, seam filtering over adjacent faces, hardware Z-fighting removal
  by a projective `depthBias`, analytic surfaces of revolution and the `C360`/`ARC` mask;
- not done: an X-ray with hidden lines (a Z test in the fragment shader against `DEPTH_TEXTURE`, recolouring
  the hidden edges pale) — only `xray` with a translucent skin exists today;
- deliberately deferred: `GCPnts_TangentialDeflection` edge discretisation for perfect arcs — it is
  `GCPnts_UniformDeflection` now so the result matches the Python reference bit for bit; revisit once the
  reference is no longer mandatory.

Performance and hardware adaptivity:
- done (0.32.0): quality presets `low`/`med`/`high` plus a fallback `safe` (software rasterizer) — one set of
  tolerances in `modules/gcode-viewer/cpp/core/quality.h`: MSAA (off/4x/8x), tessellation deflection of the skin
  and of the B-rep edge polylines (0.2/0.05/0.02 mm, 0.5 at `safe`), edge coalescing, silhouette weld and
  thresholds, normal crease angle, outline feature angle and Douglas-Peucker simplification tolerance, cone
  minimum slant and probe azimuths, silhouette arc angle and minimum span, DROP axial overlap; silhouette refresh
  step (every frame / every 4th frame), grid density 20/24 against 40/48, at `safe` an outline ribbon of 1 px
  against 2 and a deeper offset, a DPR clamp (logical pixels at `low`/`safe`) and `xray` disabled there;
- `med` repeats the numbers that were in force before the presets, so an empty environment changes nothing:
  `bench_probe` reports the same geometry (verts 136194, faces 45398, candidates 57842, edge_segs 2426,
  tan_fams 124) and the reference checks stay green. The `low`/`high`/`safe` values are proposals; only the
  deflection is measured (i5-13420H, median of three): tessellation 129.8 -> 61.3 / 416.4 / 56.0 ms and
  45398 -> 7856 / 175808 / 4132 triangles, silhouette topology 20.4 -> 3.8 / 83.3 / 2.0 ms, tangent families
  17.1 -> 3.1 / 113.2 / 1.9 ms;
- the level is chosen by the `CNC_QUALITY` environment variable (case-insensitive, like `CNC_CPP` and
  `CNC_SILHOUETTE`) and by the `root.quality` property; it is set by the entry point (`cpp/viewer/main.cpp`,
  `preview_3d.py`, `smoke_test_qml.py`, `render_probe.py`), while the `Q` key cycles the levels; the NC parse and
  the collision scan are not changed by the level, their numbers stay in the environment (`CNC_DECIMATE_TOL`,
  `CNC_ARC_DECIMATE_TOL`, `CNC_COLLISION_STEP`);
- auto-detection: the OpenGL version and renderer string via `QOpenGLContext`, a software rasterizer hint
  (llvmpipe/softpipe/Basic Render Driver) -> the `safe` level, the core count via
  `QThread::idealThreadCount`, and a reference run of a few frames after `sceneGraphInitialized`; keep the
  result in `QSettings` so the measurement is not paid on every start; force the OpenGL branch on ION;
- C++ optimisation after the port: an opaque handle on the silhouette topology instead of per-frame copies,
  a cached STEP shape in `StepEdgesGeometry`, removing the second `BRepMesh`, buffer reuse, and parallelism
  of the cold stages — the list with estimates is in [`PORT-CPP.md`](PORT-CPP.md) §11 /
  [`PORT-CPP.en.md`](PORT-CPP.en.md).

Engineering:
- split the monolith and introduce a single `dataclass` contract QML <-> Python;
- `dataclass` instead of tuples/dicts, with type hints: module-boundary structures are in place
  (`gv_types.py`, 0.13.13); core type annotations remain;
- a performance gate in `pnpm guard` via `bench_viewer.py`;
- centralized logging;
- port the compute core to C++ (the OCCT SDK directly instead of bindings) — estimate, risks and stages in
  [`PORT-CPP.md`](PORT-CPP.md) / [`PORT-CPP.en.md`](PORT-CPP.en.md).

### Material removal simulation (height map, in the background)

The line above is the wish; this is what to build, what it costs and how to check it. The simulation shows
what is left of the stock after the travelled part of the program: it continues the "hide the travelled path"
idea (`toolpath.frag`, `cutoff`), and its progress is the same `pathDist`, not a separate clock.

**Representation: a height map, not a dexel.** A grid over machine X/Y, one Z height per node (the top of the
material in the column); the cut is a `min` over the node along the path.

- The map expresses everything a surface of revolution with the tool axis along Z leaves behind: flat, corner
  radius, ball, chamfer, cone, drill — the profile already exists for the collision scan (`gv::toolProfile`,
  `cpp/core/tooling.h`). The column is computed from the lower envelope of the profile: at distance `r` from
  the axis the cut level is `z_tip + h(r)`, where `h(r)` is a table over the cutting part (up to the stick-out,
  `toolExposedLength`) and is built once per stage tool.
- The map does not express undercuts: a lollipop, a dovetail, a T-slot wider at the top than at the bottom —
  for those the map cuts material that stays on the machine. An axis not along Z (4-5 axis) is expressible by
  neither a map nor a single dexel axis. That is why the map comes first, and the shapes that widen upwards are
  not taken at all by the first version: the field is not enabled for them and the reason goes to the message
  line.
- The field lives in machine coordinates (Z up) and is drawn where the stock is: part rotation (`UI_ROTATION`)
  does not affect it, because the machine cuts in machine coordinates.
- Memory for a 200x200x60 mm stock (proposal, arithmetic): a 0.5 mm node -> 401x401 = 160 801 nodes, float32
  = 643 204 B = 628 KiB; a 0.2 mm node -> 1001x1001 = 1 002 001 nodes = 4 008 004 B = 3.82 MiB. The 60 mm
  depth adds no memory: one number per column.
- The peak is three times larger — the field in the worker, the `QByteArray` in `QQuick3DTextureData` and the
  copy in the GPU: about 1.9 and 11.5 MiB. R32F is the simple format; R16F halves it but at 60 mm depth its
  step is about 0.03 mm (the relative precision of a half) — a lever if the prototype runs into memory. Store
  the height as a depth below the stock top: the range is 0..60 mm, not an absolute Z.
- A dexel (Z intervals per column) expresses undercuts and costs differently: at least two float32 = 8 B per
  column -> already 8 MB at 0.2 mm, and 2-4 intervals on the walls -> 16-32 MB; on top of that it is not drawn
  as polygons without building the surface of the interval union. The dexel is a second step, if undercuts are
  ever needed.

**Resolution and presets.** The node size is a preset tolerance (`cpp/core/quality.h`), like the other
tolerances, and is read together with the level (`CNC_QUALITY`, the `quality` property). At `med` there is no
earlier number to repeat here — it is a proposal, and it is named as a proposal, not as a measurement.

- Proposal per level: `safe` does not draw the field (the software rasterizer level, like `xray` and MSAA
  above in this section), `low` 0.5 mm, `med` 0.5 mm, `high` 0.2 mm.
- On top there is a grid cap: the node is not finer than `max(X extent, Y extent) / N`, where N = 256 for
  `low`, 512 for `med`, 1024 for `high`. The cap only coarsens: on 200x200 it gives 0.78 / 0.5 / 0.2 mm (the
  proposed 0.5 mm of `low` does not fit into 256, those of `med` and `high` do), and on a large stock the node
  gets coarser instead of the memory growing.
- The drawn grid is the computed grid (one source of truth, no second sampling level): one vertex per node.
  The worst case is the cap itself: about 130 000 / 522 000 / 2 093 000 triangles; on 200x200 at 0.78/0.5/0.2
  mm nodes — about 130 000 / 318 000 / 2 000 000. That is no longer memory but frame cost, and the cap N is
  what sets it. Nothing is smoothed per frame, so the texture fetch is nearest: R32F linear filtering is not
  guaranteed on the OpenGL 3.3 branch, and the node coincides with the texel anyway.
- A machine that cannot afford the memory: the node gets coarser up to the cap; if even that does not fit, the
  simulation is not enabled, the reason goes to the message line (like a collision refusal through
  `reportRefusal`), and the scene keeps the plain stock box. The allocation happens once per load, not once per
  frame.

**Where the cut is computed.** A clean core next to the collision scan: `cpp/core/stock_sim.{h,cpp}` — no Qt
and no OCCT, like `core/collision`. The input is the path (`pts`, `cum`, `total`, `rapid`), the stock bounds
(`stock_bounds_` from `parseUiStock`), the cutting profiles of the stages and the node size; the output is the
field bytes.

- Only the profile up to the stick-out cuts (`toolExposedLength`); the holder never cuts — otherwise the
  simulation would hide the very collision the scan looks for.
- The step along the path is not larger than the node (proposal: half the node), otherwise a column between
  two positions is left untouched. This is the same rule the collision scan uses with its 1 mm step
  (`CNC_COLLISION_STEP`; measured 2.1 ms on Test_model, i5-10400F, `bench_probe`, phase `collision_scan`,
  CHANGELOG 0.28.0): the cut step is 2-5 times finer, and each position walks a disc of the tool radius rather
  than a holder.
- All moves are applied, rapids included: the machine really does drive the tool through the material, and
  skipping `G0` would show material that is already cut away. That a rapid into the stock is a crash is the
  collision scan's business, not the simulation's.
- It runs in the background like the scan: `QThreadPool::globalInstance()` and its own token next to
  `_collision_token`, the result returns to the GUI thread through `QMetaObject::invokeMethod`, a stale one is
  dropped. The GUI never waits: the field may lag the tool — that is the price of the background, and it has to
  be measured.
- The field is monotone along the path and accumulates from 0 to the travelled distance. A rewind does not undo
  it: the field is rebuilt from zero by the same background path, and while it is being computed the last
  complete field is shown, never a mixture.
- Nothing goes to the disk cache: the key would have to grow the stock, the node size and the profiles
  (`_cache_key`, `CACHE_VER`), and the gain is unproven — the recompute cost is measured by the same probe.

**How it is drawn.** The grid is extruded by a vertex shader from a dynamic texture — exactly as the line above
says.

- Data: `QQuick3DTextureData` (R32F, `setSize`/`setFormat`/`setTextureData`/`update`) as a controller property
  next to the geometries; in QML `Texture { textureData: path.stockField }` plus a `TextureInput` in a
  `CustomMaterial` (a property of type `TextureInput` becomes a `sampler2D` of the same name); the vertex
  shader reads the height with `texture(stockField, UV0)` and derives the normal from the neighbouring texels
  right there — the CPU computes no normals at all. This is the documented Qt Quick 3D height-map technique.
- The grid is built once per load (position 3f + texcoord 2f, the same 20 B stride as `ToolpathGeometry`), and
  only the texture changes. The update replaces the whole buffer (there is no partial upload in
  `QQuick3DTextureData`), so the cadence is ours: the field is published as a band accumulates, not every
  frame (proposal: about 10 times per second), otherwise megabytes per frame go over the bus.
- The stock stops being `#Cube`: the field draws the top face, the side walls and the bottom become a plain
  geometry of their own (about ten triangles) with the old micro-offset against z-fighting. The box top and
  the untouched field nodes lie in the same plane, and the "1.5 px" offset is not allowed for the field: it
  would lie about the geometry. Two models and two materials, because `alphaMode` cannot be switched at run
  time: the walls stay translucent and `NeverDepthDraw` as today, the cut surface is opaque and writes depth.
- There is one switch: `showStockVisible` (the S key, the "Габарит заготовки" button) shows both the box and
  the field. There is no separate flag: a pile of toggles is already named as debt (§12). The outline and
  silhouette ribbons are untouched — they already run the depth test.
- An opaque cut surface hides the path under the material; that is the point of the simulation, but it changes
  the familiar picture (today the box is deliberately `NeverDepthDraw`). The decision is made by a prototype
  and a screenshot in `img/`, not by default.

**What the simulation must not do.**

- It must not slow the orbit: no per-frame work, no per-frame texture upload, no JS loop over hundreds of
  thousands of vertices. But the grid is triangles in the frame, and its cap above is part of the orbit budget
  (`PORT-CPP.md` §11: the orbit is cheap, the time goes into the cold load and heavy features).
- It must not touch the bit-exact contract: the field is in neither `PathData`, nor `PackedStream`, nor the
  geometry buffers, nor `oracle_manifest.json`; `prepareOrdered`, the parse and the packing stay as they are.
  The field accumulates float minima, so sha256 is no use for it at all; if a Python mirror ever appears, it
  must be compared with a millimetre tolerance, and that exception to the general rule is named, not implied.
- It must not require Python: the computation is in the C++ core and QML draws it. The Python entry points
  (`preview_3d.py`, `smoke_test_qml.py`) stay without the field: QML must survive a missing property (no field
  shown, the load unchanged). The cache and the reference checks never see the field.

**Checks and what to measure.**

- These stay green: `test_gcode_core.py` (the path sha256), `test_normals_golden.py`, `test_geometry_core.py`
  (the six geometry buffers, their `stride` is frozen), `test_controller_cpp.py`, `test_cpp_bridge.py`,
  `oracle_dump.py`, the offscreen `loaded OK` and `RESULT: OK`. The seventh geometry is not part of the six-way
  comparison set.
- The new check of the same layer is a `cpp/stock_probe.cpp` probe with a thin wrapper, like the others: it cuts
  a synthetic stock with a known path and compares the field against analysis, not against a sha256. Proposed
  cases: (1) a plunge with no motion — the field equals the profile `h(r)`, tolerance about 1e-12 (the value at
  a node is exact); (2) two plunges — a per-node `min`; (3) a pass to the side — nodes outside the swept band
  are untouched; (4) the field is never above the stock top and never rises along the path; (5) a recompute
  after a reset gives the same bytes — the rewind rests on that determinism. A millimetre tolerance is needed
  only where a node does not coincide with the truth (a curved wall between nodes), and it is derived from the
  node size, not tuned until green.
- Measurements: `bench_probe` — a new phase (`stock_sim`) next to `collision_scan`; the frame with and without
  the field, per level — the probe's `frame` lines and `bench_viewer.py` (`hot` mode), on weak hardware
  mandatory. The live numbers to watch: the collision scan 2.1 ms at a 1 mm step (i5-10400F), the NC parse 174
  ms and `ngc_parse` 69.7 ms (i5-13420H), the cold load about 690 ms, the mesh silhouette 0.0791-0.0868 ms,
  the skin 45398 triangles (`med`), `stock_px 67235` (`render_probe.py`).
- Proposals, not measurements: the 0.5/0.5/0.2 mm nodes, the caps 256/512/1024, the step = node/2, the R32F
  format, the cadence about 10 Hz, `safe` not drawing at all, refusing the lollipop and the dovetail, the 628
  KiB and 3.82 MiB of memory, the cap triangles 130 000/522 000/2 093 000. `render_probe.py` prints
  `stock_px` — once the surface is drawn that number will move, and the move is recorded deliberately rather
  than silently re-approved.
- Not taken before a prototype: whether the ION samples an R32F texture in the vertex stage on the OpenGL 3.3
  branch; what a whole-texture `update()` costs on a narrow bus; what an opaque surface does to path visibility
  (a screenshot); whether to draw the whole grid or only the touched rectangle (the latter is cheaper in
  triangles but needs a joint with the box top face); whether R16F is needed. The prototype is a measurement
  only; it does not enter the tree.

## 14. Checks and tests

Before committing (VERSIONING.md, section 5):
```
python -m py_compile modules/gcode-viewer/mock_backend.py
set QT_QPA_PLATFORM=offscreen
python modules/gcode-viewer/preview_3d.py "data/cad-testdata/Test_model.ngc"     # offscreen -> loaded OK
python modules/gcode-viewer/smoke_test_qml.py                      # offscreen -> RESULT: OK
python modules/gcode-viewer/test_tangent_policy.py                 # silhouette policy
python modules/gcode-viewer/test_normals_golden.py                 # normals
```

Performance — `bench_viewer.py` (`hot` mode runs the interactive path only). Numbers enter a comment only when
they are measured.

## 15. Directions for panel development on the 3D base

Five panel features built on the viewer scene and data (the panel is `apps/react-interface/lib/cnc`). For each: the
pain today, what the operator does, and what the code rests on — and what is still missing.

1. **Interactive 3D probing (click a face).**
   - Pain: the probing macro is picked from a picture and the sizes are typed by hand; a typo in the approach breaks a
     ruby stylus.
   - Operator: clicks a face of the loaded STEP part; the base recognises the geometry (a hole's cylindrical wall ->
     the diameter, two parallel faces -> a boss width), picks the matching probing macro, fills the approach and shows
     the proposed probe path in 3D before Cycle Start.
   - Rests on: the analytic faces are already extracted — `step_import.py` (`load_step_model`, tessellation,
     `GeomAbs_Plane`), `step_surfaces.py` (`load_step_surfaces`, `GeomAbs_Cylinder`/`GeomAbs_Cone`), the
     `CylinderSurface`/`ConeSurface` types in `gv_types.py`, the C++ mirror `cpp/occt/step_surfaces.cpp`; the probing
     macros — `PROBE_CYCLES`/`computeProbeResult` (`lib/cnc/probe.ts`). Required: a ray face pick in
     `GcodeViewer3D.qml` (today `MouseArea` is orbit and pan, there is no ray), mapping a face to a macro, and drawing
     the proposed probe path.

2. **"Time machine" and safe restart.**
   - Pain: "run from line" after a broken cutter is a gamble.
   - Operator: scrubs the 3D trajectory to the break point; the base reads the context (active tool, spindle rpm, work
     offset), builds a safe approach tail (raise Z to machine zero -> spindle and coolant -> rapid X/Y above the start
     point -> descend to clearance -> continue) and draws the tail in 3D before the start.
   - Rests on: the path data and stages exist — `PathData`/`Stage` (`gv_types.py`), `sampleAt`/`stages`
     (`controller.py`), `setToolStage`/`_stage_tools`, the stick-out `_tool_exposed_length` (`tooling.py`); the
     background pass and token — `_scan_collisions`/`_collision_token` (`collision.py`, `controller.py`), the stock
     field — `cpp/core/stock_sim.{h,cpp}`. Required: the viewer parses neither the `S` speed nor the work offset
     `G54`..`G59` (`gcode_parser.py` knows motion and planes only), and the generation and drawing of the approach tail
     are missing too.

3. **Selective machining (a 3D window).**
   - Pain: otherwise a 40-minute program is run in full, or a CAM programmer is asked to cut out a fragment.
   - Operator: holds Shift and drags a 3D box around the problem area; the base filters the trajectory and keeps the
     program lines that intersect the volume — only that corner is re-machined.
   - Rests on: the path geometry and bounds exist — `PathData.pts`/`cum`, `Aabb` (`gv_types.py`), the travelled
     distance `pathDist` (`GcodeViewer3D.qml`). Required: selecting a volume in the scene and a filter by volume, and
     the link from a segment to a program line must be built from scratch — the viewer keeps no line numbers, and
     "linking 3D and the NC line" is listed as not done in §13.

4. **Trajectory heat map (load).**
   - Pain: on chatter the operator turns the feed override blindly.
   - Operator: after the first part sees the trajectory coloured by load (green — normal, yellow — rising, red — peak
     and chatter) from the spindle load and the actual feed, and edits the program.
   - Rests on: the trajectory and the programmed feed exist — `toolpath.vert`/`frag` already colours fragments by
     `cutoff`/`pathDist`, `speedAt` (`controller.py`). Required: no layer reads the live machine state;
     `spindle.0.load` and the actual cutting speed exist only as a list of pins in `HAL_GROUPS`
     (`lib/cnc/service-data.ts`) — a static reference, not a data source; the panel (`MachineState`) is a mock with
     `feedOverride`, and the HAL link is not built.

5. **Ctrl+Z for the offsets.**
   - Pain: a single wrong "zero X" in the wrong tab loses the part datum for good.
   - Operator: keeps a history (for example the last 50 changes) and rolls the work offset and tool length tables back,
     for example to the state of ten minutes ago.
   - Rests on: the offsets and tools live in the panel state: `WorkOffset`/`Tool` (`lib/cnc/types.ts`),
     `INITIAL_OFFSETS` (`lib/cnc/data.ts`), the `OFFSET_TEACH`/`OFFSET_CLEAR`/`ZERO_WORK` edits in the reducer
     (`lib/cnc/machine.ts`); a history exists only for MDI (`mdi.history`, `MDI_HISTORY_SIZE`). Required: the offset
     and tool edit history itself and its rollback.

6. **Virtual fixture and vise lookup.**
   - Pain: the cutter dives into hardened jaws or clamps because the CAM model of the fixture was missing or
     stood in the wrong place.
   - Operator: loads STEP models of the vise, chuck and clamps into the station, touches the corner of the real
     vise with the probe — the 3D fixture model snaps to those coordinates, and the look-ahead pass recomputes
     and marks red where the holder would reach the jaws.
   - Rests on: the touch is already modelled — `Touch`/`probeLocalPosition`/`localToMachine`/`PROBE_CYCLES`
     (`lib/cnc/probe.ts`), the cycle run — `PROBE_STEPS` and the measure step in `machine.ts`; STEP is read
     (`load_step_model`, `step_import.py`), the collision scan — `_scan_collisions`/`_holder_profile`
     (`collision.py`, `tooling.py`). Required: a slot for a second STEP model (the viewer has one today,
     `_model_path` in `controller.py`), snapping the fixture model to the touch, and a "holder against fixture"
     scan: today `_scan_collisions` compares the holder cylinders with the stock bounds, not with the fixture body.

7. **The "ghost" of the future (look-ahead projection).**
   - Pain: on a first part the operator holds the e-stop and stares through the coolant — the next move is not
     visible.
   - Operator: sees a semi-transparent ghost tool running along the trajectory 3-5 s ahead of the real one, so
     the next manoeuvre is visible before the machine makes it in iron.
   - Rests on: the whole program is parsed ahead (`parse_gcode` -> `_prepare_ordered`, `path_prep.py`), the path
     is fully in the samples (`_sample_pts`/`cum`), the position comes from `sampleAt`/`speedAt` (`controller.py`),
     and QML already drives `pathDist` and `path.playing` (`GcodeViewer3D.qml`). Required: a second tool mesh at
     its own `sampleAt(pathDist + lead)`, where the lead is the path covered in 3-5 s by `speedAt`, and its
     semi-transparent drawing.

8. **Editing the G-code by dragging in 3D.**
   - Pain: to raise a rapid the operator opens a text editor and looks for `G0 Z5.0` among tens of thousands of
     blocks.
   - Operator: takes a red rapid line in the 3D view and drags it up; the station finds that block in the program,
     rewrites the coordinate and updates the trajectory at once.
   - Rests on: the panel already keeps a block number per segment — `Segment.line` (`lib/cnc/toolpath.ts`,
     `buildToolpath`), block parsing — `parseWords` (`gcode.ts`), line editing — `setLines` (`files.ts`),
     replacement — `SEARCH_REPLACE_ALL` (`prog.ts`); but the panel trajectory is 2D (`project`, `PathView`) and
     the viewer keeps no block numbers at all. Required: a ray pick in the scene, the "scene segment -> program
     block" link (listed as not done in §13) and a drag handle that rewrites the coordinate.

9. **Feed optimiser from the first pass.**
   - Pain: a safe feed is used everywhere, including where the load is 10 per cent, and the machine loses time.
   - Operator: on the first part the station records spindle load and vibration against coordinates, then a
     button splits long G1 runs and raises `F` where the cutter was cutting air and lowers it before a hard
     plunge — the second part runs much shorter.
   - Rests on: the feed and its override live in the panel state — `motion.feed`, `motionSpeed` (`coords.ts`),
     `feedOverride` (`machine.ts`); block editing — `setLines`/`SEARCH_REPLACE_ALL` (`files.ts`, `prog.ts`).
     Required: a load source (today `LoadRow` in `side-status.tsx` draws a sine of time, and the
     `motion.current-vel`/`spindle.0.speed-out` pins in `HAL_GROUPS` are a static reference), recording load
     against coordinates, and splitting long G1 runs with a new `F`.

10. **Tracking the actual removed material volume (MRR).**
    - Pain: a timer cannot see that 30 minutes at 5 per cent stepover is not the same as 5 minutes at full depth.
    - Operator: the station computes the volume removed by each tool and debits the life in cubic centimetres of
      chips instead of spindle minutes.
    - Rests on: the life and the run time — `Tool.life` (`types.ts`), `toolTime` on `TICK` and `TOOL_RESET_LIFE`
      (`machine.ts`), the `TOOL_LIFE` report (`prog.ts`); the stock height field is already computed —
      `StockField`/`simulateStock` (`cpp/core/stock_sim.{h,cpp}`), the tool of a stage — `_stage_tools`/`Stage`
      (`controller.py`, `gv_types.py`). Required: volume from the height field per stage (or OCCT booleans) and
      debiting `Tool.life` in cm³.

11. **Emergency recovery wizard.**
    - Pain: after a following error the axes are disabled and the cutter stands in metal, and the operator
      guesses — guessing breaks the cutter.
    - Operator: an interactive wizard with 3D animation walks him out step by step: a short spindle reverse to
      clear chips, releasing the Z brake, raising with the handwheel; dangerous moves (X/Y while Z is still
      inside the part) are blocked.
    - Rests on: alarms and levels — `Alarm`/`pushAlarm` (`types.ts`, `messages.ts`), the states `ESTOP`/`ALARM`
      and `RESET`/`ESTOP` (`types.ts`, `machine.ts`), the clamp and spindle — `toolClamped`/`spindleOn`, the
      handwheel run — `mpgReducer`/`MPG_HANDWHEEL` (`mpg.ts`). Required: the wizard itself and the move-blocking
      rules; the axis following error exists only as the `FERROR`/`MIN_FERROR` strings and the "Axes: following
      error" pin group (`service-data.ts`), and the handwheel today turns the dry run only.

12. **Tool data import by QR/RFID.**
    - Pain: the correction is typed off a paper slip from an optical presetter — one zero too many (12.0 instead
      of 120.0) drives the spindle into the table.
    - Operator: scans the code on the holder; the station pulls the exact length and diameter from a database and
      loads the 3D profile of a complex tool (taper, bull-nose, lollipop), and the manual entry goes away.
    - Rests on: the tool table — `Tool` (`types.ts`), presetter measurement — `TOOL_PROBE` (`machine.ts`) and
      `sensorLength`/`BROKEN_TOL_MM` (`tool-check.ts`); the tool profile is already built from the `UI_TOOL`
      fields — `_tool_profile` (`tooling.py`: FLAT, BALL, BULL, CHAMFER, TAPER, DRILL, TAP, LOLLIPOP, DOVETAIL, PROBE),
      the fields are parsed by `parse_ui_tool` (`ui_tags.py`). Required: code input (QR/RFID), a tool database
      and turning its record into `UI_TOOL` fields; there is neither a scanner nor a database in the tree.

13. **Kinematically accurate time prediction.**
    - Pain: CAM cannot know the machine's accelerations and path blending (`MAX_ACCELERATION`, `G64`/arc
      blending), so its estimate never matches the stopwatch.
    - Operator: the station runs the program through the controller's planner in the background, gives a cycle
      time that matches the stopwatch, and recomputes it when the operator turns the physical feed override.
    - Rests on: the panel already shows a remaining estimate — `remainingLength` (`toolpath.ts`) and `restMs` in
      `program-screen.tsx` divide the remaining path by `programFeed` (`machine.ts`), while the `feedOverride`
      correction acts on the current move only (`motionSpeed`, `coords.ts`); the parser reads the modal `F`
      (`feed`/`_to_mms`, `speedAt` — `gcode_parser.py`, `controller.py`).
      Required: a planning pass over accelerations and blending; no layer reads them (`gcode_parser.py` parses
      motion, planes, G90/91 and cycles only, and `MAX_ACCELERATION`/`STEPGEN_MAXACCEL` exist only as reference
      parameters in `service-data.ts`), and the estimate is not recomputed from the feed override.

**What is closest to being implementable.** Closest are items 3 and 5: selective machining already has the path
geometry and bounds, and the rollback has the panel state ready. Farthest is item 4: no layer reads the live machine
state, and the spindle load exists only as a reference string. Items 1 and 2 stand on ready STEP faces and path data
but run into what is missing: a ray face pick, `S` and offset parsing, and the approach tail generator.

**What is closest among items 6-13.** Closest is item 7: the path is parsed ahead and the position already comes
from `sampleAt`, so only a second tool mesh with a lead is needed. Farthest is item 13: an accurate time needs a
planner over accelerations and blending, and no layer reads them — `MAX_ACCELERATION` lives only as a reference
parameter. Item 9 runs into the same missing live-state layer as item 4.
