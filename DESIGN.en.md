# DESIGN.md — 3D viewer for NC programs (`gcode-viewer`)

The source of truth is the code and its behavior (CLAUDE.md), not this description: the document explains the
design and may lag behind. Change this file together with behavior. Russian version — [DESIGN.md](DESIGN.md);
edit both languages in the same pass.

## 1. Purpose and place in the product

`gcode-viewer` is the third of the product's four projects (manifest — [projects.json](projects.json)):
interactive 3D preview of a machine control program. Directory — `modules/gcode-viewer`. Stack: QML +
QtQuick3D + C++ (Qt 6, OCCT). No Python layer is left in the repository.

Dependencies:
- Qt 6 (Core, Gui, Qml, Quick, Quick3D) — the window, the controller QML module and the geometries;
- OCCT (the OpenCASCADE SDK directly) — STEP reading, tessellation, analytic surfaces;
- neither numpy nor the Python bindings (`cadquery-ocp`, PySide6) are in the tree: everything is computed in C++.

The viewer's contract with the post processor is the `UI_*` tags in NC comments (`UI_STOCK`, `UI_TOOL`,
`UI_OP`, `UI_HOLDER`, `UI_ROTATION`, `UI_ORIGIN`, `UI_CAD_MATRIX`). The tags come from `cad-testdata`
(`data/cad-testdata/linuxcnc_ui.cps`); when a tag changes, edit the post and the parser in the same pass (VERSIONING.md,
section 7).

## 2. Entry points

- `build/viewer/gcodeviewer` (`cpp/viewer/main.cpp`) — the canonical viewer launch: a `QQuickView` over
  `GcodeViewer3D.qml`, controller QML module `MockBackend` from `build/qmlbackend`. Build — README,
  "Building and running the viewer (C++)".
- Entry-point keys: `--smoke` (start-up check, `RESULT: OK (parse-level)`), `--frame <file.png>` (a frame in
  PNG: image size and the count of non-background pixels). A plain launch loads a program and prints segments.
- `preview_3d.qml` — a 1024x600 `Window` holding `GcodeViewer3D` (data, edited by hand).
- The Qt-interface preview (`apps/qt-interface`) is another project; it has no launch point in the repository.

Headless run: set `QT_QPA_PLATFORM=offscreen` (set by `--smoke` itself; `--frame` takes the platform from
outside, because only a live platform draws the Quick3D scene, while offscreen returns a frame without 3D).

## 3. Architecture: layers

```
C++ (gvcore): parser and preparation (pure, no Qt)
        G-code parsing, decimation, buffer packing, stock removal
   |  QThreadPool + signals
C++ (MockBackend): controller GcodeToolpath (QObject) and QQuick3DGeometry shapes
   |  QML properties/slots
QML:    GcodeViewer3D.qml — scene, camera, materials, buttons, animation
   |
GPU:    QtQuick3D draws triangles and lines
```

- **Pure core.** `gv::parseGcode` and `gv::prepareOrdered` (`cpp/core`) do not touch Qt and are thread-safe:
  they can run in any thread. This is what lets the load leave the GUI thread.
- **Geometries.** Each is a `QQuick3DGeometry` subclass (`cpp/qt/gv_geometry.cpp`); C++ supplies the binary
  vertex buffer, primitive type, attributes and bounds, and QtQuick3D draws it (one shared pattern:
  `clear -> setVertexData -> setStride -> addAttribute -> setBounds -> update`).
- **Controller.** `GcodeToolpath` owns the geometries and the properties QML sees (`center`, `radius`,
  `moveLength`, `stages`, `collisions`) and the slots (`sampleAt`, `speedAt`, `isRapidAt`, `refreshTangents`,
  `setToolStage`, `load_gcode_async`).
- **QML.** Describes the scene and the interaction; machine state lives in the C++ controller.

## 4. Data model and the QML contract

The controller exposes finished data; QML never receives the per-point path lists:
- `sampleAt(d)` — interpolated tool position at path distance `d` (binary search over `cum` plus linear
  interpolation);
- `speedAt(d)` / `isRapidAt(d)` — speed and rapid flag of the segment;
- `stages` — program operations as normalized `[start, end]` path ranges;
- `collisions` — collision ranges with a representative scene point.

A group of values that travels together is a named type (CLAUDE.md): the module-boundary structures live in
`cpp/core/gv_types.h` (`PathPoint`, `Stage`, `Aabb`, `PackedEdges`, `PathData`, `CylinderSurface`/`ConeSurface`,
`TangentFamilies`, `MeshTopology`, `ProfilePoint`/`HolderSection`). A dict remains only where QML reads it
(`toolInfo`, `stages`, `collisions`, `toolCheck`): that is the QML contract, not debt.

## 5. Coordinate system

The machine works in (X, Y, Z) with Z up; the scene uses Y up. One permutation covers the whole code
(`gv::toScene`): machine `(X, Y, Z) -> scene (X, Z, -Y)`. Units are millimeters, the scene is 1:1 with the part,
so "scene units == machine mm". Every machine <-> scene conversion goes through this function.

## 6. Threads and asynchrony

Loading (`load_gcode_async`) starts a task in `QThreadPool::globalInstance()`:
1. parse the program and prepare the buffers;
2. signal `toolpathReady` -> the GUI applies the toolpath;
3. read the model (STEP/STL) in the background;
4. signal `meshReady` -> the GUI applies the part.

Each result carries a token (`_load_token`, `_edge_token`, `collisionToken`, atomics): a stale result (the user
opened another program) is dropped. The STL outline, the collision scan and the stock removal run as their own
pool tasks.

There is one correctness mechanism: **the `TopoDS_Shape` is read once** per load (`cpp/occt/step_bundle`), and
the skin, the edges and the analytic surfaces all work on it - no OCCT calls run in parallel on one shape, so no
global lock around OCCT is needed.

## 7. Disk cache

There is no cache: the `.npz` cache (format version, `CNC_NO_CACHE`, trimming by a size cap) was a Python tool
and was removed with the Python layer. It was never ported to the C++ path: `bench_probe` measures the recompute
cost, and the gain was never proven.

## 8. Part model: STEP and STL

Next to the program, a `.step`/`.stp` is looked up, otherwise a `.stl`.

**STEP (primary path).** The B-rep is read once (`gv::stepBundle`) and used for both the skin and the analytic
data. `gv::loadStepModel` (`cpp/occt/step_import.cpp`) tessellates the surfaces (deflection 0.05 mm) and
extracts the true B-rep edges (cylinder seams are filtered out as form lines). `gv::smoothFaceNormals`
(`cpp/core/normals.cpp`) builds crease-aware normals.
`gv::loadStepSurfaces` (`cpp/occt/step_surfaces.cpp`) separately collects analytic cylinders and cones (axis,
radius, arc) — the silhouette
needs them.

**STL (fallback).** A triangle soup without topology; the outline is a dihedral-angle heuristic
(`gv::featureEdges`, threshold 20 degrees). Its edges arrive as chains of sub-millimetre links (the median
edge of Test_model.STL is 0.07 mm), so the chains are welded and simplified (`gv::simplifyEdgeChains`,
tolerance 0.15 mm): a ribbon is built from the two ends of a segment, and on a sub-pixel link its direction
is noise. **Limitation:** the heuristic only sees sharp transitions. B-rep face boundaries where the surfaces
meet tangentially (blend boundaries, seams) cannot be told from triangulation edges by angle, so the STL
outline carries fewer internal contours than the STEP one - a property of the fallback, not a defect. The
threshold was swept: 20 -> 2 degrees adds about 290 segments and does not reach the STEP network (6825 mm),
while below 1.5 degrees the outline explodes tenfold on curved-surface facets.

**Shell orientation.** The sign of a normal sets the shading of a face, so the shell is turned outward once
per model - `gv::orientShellOutward` (for a closed solid the area-weighted normals point away from the
centroid). A per-face flip about the centroid is not applied: on concave regions it turns the normals of
pockets and holes inward. Guarded by `step_probe --manifest` (the `step.normals` digest).

## 9. Outline and silhouette

The classic CAD look is a shaded part plus two kinds of contours:
- **static B-rep edges** — real face boundaries, independent of the view angle;
- **dynamic silhouette (tangents)** — tangent generatrices of holes and cylinders, dependent on the camera
  eye.

Hidden lines are removed by the GPU depth test: the skin is opaque and writes depth, and the contours render
with a small shift toward the camera. That gives hidden line removal for free, with no CPU visibility pass.

**Ribbons instead of GL_LINES.** Real GL_LINES are always 1 px and antialias poorly; each edge is drawn as two
triangles (`HLRLineGeometry`) whose width the vertex shader keeps at a constant number of pixels.

**Tangent families.** `gv::tangentFamilies` (`cpp/core/tangents.cpp`) is precomputed once per load: cylindrical
faces are grouped by axis and radius; arc coverage is measured on the skin mesh (`ARC`) and generator lines that
coincide with a static edge are collected (`DROP`). `gv::tangentFromFamilies` computes the tangents on every
camera move (fast, under a millisecond), dropping duplicates of the static edges. This complexity is justified:
the naive silhouette produced "double lines that drift off the contour".

## 10. Tool and holder

The tool is built procedurally from `UI_TOOL`: `gv::toolProfile` (`cpp/core/tooling.cpp`) builds a 2D profile
`(radius, height)` for the cutter type (flat, ball, bull, chamfer/taper, drill, ...), and `ToolGeometry` revolves
it into a solid of revolution. The outline is an inverted hull (an enlarged copy that renders only its far
faces). The holder is a coarse ER20 model (nut plus body) from `gv::holderProfile`; the stick-out may come from
`UI_HOLDER` (`gv::toolExposedLength`). The tool position is `sampleAt(pathDist)`.

## 11. Animation, collisions, tool check

- **Animation.** `FrameAnimation` fires once per frame and reports the real frame time, so playback speed does
  not depend on FPS. Speed is 1:1 with the machine: feed `F` (mm/min -> mm/s), rapids at a fixed rate.
- **Hiding the traveled path.** A custom material `toolpath.vert/frag` discards fragments whose path parameter
  is below the progress (`cutoff`); there is a fade of the far section and a stages mode.
- **Collisions.** `gv::scanCollisions` (`cpp/core/collision.cpp`) is driven in the background by the controller:
  a check of the holder cylinders against the stock bounding box at a 1 mm step; the result becomes red markers
  in the scene.
- **Tool check.** `toolCheck` (`cpp/core/collision.cpp`) compares the program tool with the controller tool table
  (numbers, stick-out). It is a skeleton: the table is fed from outside (`setMachineTools`).

## 12. Known problems and technical debt

Architecture:
- the Python layer is gone entirely: the former `mock_backend` facade and the modules it split the monolith into
  are no longer in the repository; the C++ layer is built from the same split (`PORT-CPP.md` §7).
- `GcodeViewer3D.qml` (about 1900 lines) holds JS camera, projection and stage logic; it belongs in backend
  slots.

Code against the CLAUDE.md rules:
- module-boundary structures are no longer tuples/dicts: they live in `cpp/core/gv_types.h`; a dict
  (QVariantMap) remains only where QML reads it (`toolInfo`, `stages`, `collisions`, `toolCheck`) — a contract,
  not debt;
- the display toggles (`showStockVisible`, `showBrepEdges`, `stockGhost`, ...) are a run of booleans, not a
  mode; the part view and the stock view are modes of their own (`partMode`, `stockView`);
- many magic numbers without a name or unit;
- development-history comments (the rules forbid them);
- mixed comment languages (Russian and English).

Checks:
- probes compare array digests against the reference (`--manifest`) rather than private functions: the
  fragility of the former Python tests is gone, but the probe prints no "why" on a mismatch;
- `refreshTangents` computes the silhouette inside `try`/`catch (std::exception)` and prints a refusal through
  `warnDeferred` (the former `try/except Exception: pass` is gone); it publishes the segments outside `try` — a
  failure of `setSegments` itself is not treated as a silhouette failure;
- the live frame checks that counted pixels (solid skin and ribbon width) are removed with the Python layer:
  the pixel count was a Python-era check, while the geometry is proven by the analytic cases (`stocksurface`,
  `stockshell`, `stock_bind`). The acceptance picture is taken by `gcodeviewer --frame`.

Remaining after the C++ port (open, with measurements):
- the panel `apps/qt-interface/main.qml` (and `preview.qml`) compares hotkeys by
  `nativeVirtualKey`, which the QML `KeyEvent` type does not expose at all (verified in the viewer: the
  property is `undefined`), so the panel hotkeys depend on the layout; fixing it needs a hand edit of
  `preview.qml` (the generator is gone) and its own `qt-interface` version;
- the C++ entry point `gcodeviewer.exe` does not accept an absolute NC path with Cyrillic characters:
  the arguments are read as `QString::fromLocal8Bit(argv[i])`, so `GetCommandLineW`/`CommandLineToArgvW`
  is needed;
- the C ABI bridge `gvbridge.dll` and the shim `gvqtshim.dll` still build, but their only consumer (the Python
  ctypes shim) is gone: the targets await a decision - drop them or keep them as an experiment stub;
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
- the glass stock (`stockGhost`, the default) blends across the whole stock area, and a transparent part
  (`partMode=1`) adds a full-screen overdraw on top of that;
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
- `gvqtgeom` (the C++ Qt geometries) is wired into the `MockBackend` QML module: there are no Python geometries,
  and the classes match the old contract (`stride` is frozen by the digests); `Eigen` is not used in the C++ at
  all (bit-for-bit requires a hand-written summation order).

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
  the hidden edges pale) — only a transparent skin exists today (`partMode=1`, the former X-ray view);
- deliberately deferred: `GCPnts_TangentialDeflection` edge discretisation for perfect arcs — it is
  `GCPnts_UniformDeflection` now so the result matches the Python reference bit for bit; revisit once the
  reference is no longer mandatory.

Performance and hardware adaptivity:
- done (0.32.0): quality presets `low`/`med`/`high` plus a fallback `safe` (software rasterizer) — one set of
  tolerances in `modules/gcode-viewer/cpp/core/quality.h`: MSAA (off/4x/8x), tessellation deflection of the skin
  and of the B-rep edge polylines (0.2/0.05/0.02 mm, 0.5 at `safe`), edge coalescing, silhouette weld and
  thresholds, normal crease angle, outline feature angle and Douglas-Peucker simplification tolerance, cone
  minimum slant and probe azimuths, silhouette arc angle and minimum span, DROP axial overlap; silhouette refresh
  step (every frame / every 4th frame), grid density 20/24 against 40/48, at `safe` an outline ribbon of 0.8 px
  against 1.5 and a deeper offset, a DPR clamp (logical pixels at `low`/`safe`);
- `med` repeats the numbers that were in force before the presets, so an empty environment changes nothing:
  `bench_probe` reports the same geometry (verts 136194, faces 45398, candidates 57842, edge_segs 2426,
  tan_fams 124) and the reference checks stay green. The `low`/`high`/`safe` values are proposals; only the
  deflection is measured (i5-13420H, median of three): tessellation 129.8 -> 61.3 / 416.4 / 56.0 ms and
  45398 -> 7856 / 175808 / 4132 triangles, silhouette topology 20.4 -> 3.8 / 83.3 / 2.0 ms, tangent families
  17.1 -> 3.1 / 113.2 / 1.9 ms;
- the level is chosen by the `CNC_QUALITY` environment variable (case-insensitive, like `CNC_CPP` and
  `CNC_SILHOUETTE`) and by the `root.quality` property; it is set by the entry point (`cpp/viewer/main.cpp`),
  while the `Q` key cycles the levels; the NC parse and
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
- a single QML <-> C++ contract: module-boundary structures live in `cpp/core/gv_types.h`, QML slots are typed;
- a performance gate in `pnpm guard` via `bench_probe`;
- centralized logging;
- the compute core is ported to C++ (the OCCT SDK directly instead of bindings) — estimate, risks and stages in
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

- Proposal per level: `safe` does not draw the field (the software rasterizer level, like MSAA above in this
  section), `low` 0.5 mm, `med` 0.5 mm, `high` 0.2 mm, `safe` 1.0 mm.
- The `safe` node is measured even though the field is not drawn on it (the weak-machine level): on
  Test_model, i5-13420H — 39.5 ms (repeat 35.2), a 151x101 grid, a 61 004 B field; for comparison the 0.5 mm
  node takes 191.2 ms and a 301x201 grid, the 0.2 mm one 2488.4 ms and 751x501. `CNC_STOCK_CELL` overrides
  the preset step without changing the level.
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
  `collisionToken`, the result returns to the GUI thread through `QMetaObject::invokeMethod`, a stale one is
  dropped. The GUI never waits: the field may lag the tool — that is the price of the background, and it has to
  be measured.
- Live publication: while the program plays, the field reflects the cut up to the current `pathDist` — the same
  counter QML plays with. The tick runs every 100 ms (a `Timer` in QML; it does not run outside playback, and
  a stop and a rewind are caught up in one call). The field is monotone, so the tick continues the walk from
  the last whole move instead of recomputing; backwards along the path (`target < cursor`) the walk resets the
  field and starts over. Measured (Test_model, 0.5 mm node, i5-13420H): a full pass 0.43-0.44 s, the mean tick
  0.0074-0.0078 ms over 53 915 ticks of the whole program at 1:1 (the longest 0.5-1.0 ms), a half-path rewind
  200-232 ms; at a 0.2 mm node the full pass is 4.9-5.1 s, and the catching-up tick after a quarter-path
  rewind in the controller 3.97-4.13 s (that is not 10 Hz but the cost of a full prefix recompute).
- Integrity before freshness: the published field is replaced only whole and only when the walk has reached its
  target. While a tick or a rewind is being computed, the screen keeps the last complete field
  (`stockHasField`, state `computing`), so the scene neither empties nor shows a mixture. The first pass of a
  program has no earlier field — the stock box is shown. A cancelled pass publishes nothing and keeps the same
  last complete field. Changing the program drops the field: it belongs to the former path, stock and tools.
- Undercut: `StockCutter::undercut` — a profile wider at the top, plus the `UI_TOOL` types `LOLLIPOP`,
  `DOVETAIL`, `T_SLOT`/`TSLOT`, which `toolProfile` builds as a straight cutter (the shape is already lost).
  The program as a whole is not expressible by the map: there is no field at all (not an empty one), the state
  is `refused`, the reason goes to the refusal line (`reportRefusal`: qCWarning and stderr), and the box is
  shown. One such stage makes the whole field a lie, not just its own patch.
- Depth rule: the coincident pixel is won by the removal surface, not by the part skin. For a finished program
  the cut surface is the skin, and at the contact points they lie in one plane; in the simulation the cut
  surface is the result and the skin (STEP/STL) is reference geometry. The tie is decided by one shift of the
  DEPTH ONLY in the vertex shader (`stockDepthOffset`, `clip.z += offset * clip.w`): one handle for the field
  top (`stocksurface.vert`) and for the walls and bottom (`stockshell.vert`), because the stock bounds equal
  the part bounds — not only the pocket bottom coincides with the skin but the outer stock walls with the part
  faces. The vertex positions do not move, so the bounds and the cut depth do not lie. The value is -3e-5 NDC;
  measured (a 1024x600 frame, Test_model, solid skin, toolpath lines off, camera yaw 35 pitch -32, stock area
  66 126 px; the pixels of the tie are where the skin is visible but would not be without it): 1e-6 - 803,
  1e-5 - 365, 3e-5 - 7, 1e-4 - 7. The spread of the tie is not float32 noise but geometry: the cut wall in the
  field is slanted by half a cell (0.5 mm at a 0.5 mm node) while the part wall is vertical. Caveat: the offset
  is constant in NDC, so in millimetres it depends on the distance — 3e-5 is 1.0 mm at 341 mm (1 % of the
  100x160x30 stock) and 2.7 mm at 900 mm; 1e-4 would give 3.4 mm at 341 with no gain in the tie (the same
  7 px). The offset does not change the screen position, only clip z.
- The solid part skin is the default, and its look depends only on the part mode (`partMode`: solid ->
  transparent -> hidden): neither the stock nor the field affects it. The former rule "the skin turns into a
  ghost while the field is on screen" is cancelled — it made the part transparent without being asked, and the
  cut metal reads anyway. Measured (a 1024x600 frame, Test_model, toolpath lines off): at a 1e-6 shift the
  copper cut is 11 967 pixels with the solid skin against 12 323 with no skin at all (3 %), and with the
  opposite shift sign 4 578 remain — the skin would win the coincident pixel, hence the shift sign is minus.
  The price of the transparent skin is named: Blend-overdraw, a more expensive frame than the solid one on a
  weak GPU.
- Field memory: 242 004 B at a 0.5 mm node (the Test_model stock 150x100 mm, a 301x201 grid) and 1 505 004 B
  at 0.2 mm (751x501); the peak is three buffers: the core field, the `QByteArray` for the texture and the GPU
  copy.
- Nothing goes to the disk cache: the key would have to grow the stock, the node size and the profiles, and the
  gain is unproven — the recompute cost is measured by the same probe.

**How it is drawn.** The grid is extruded by a vertex shader from a dynamic texture — exactly as the line above
says.

- Data: `QQuick3DTextureData` (R32F, `setSize`/`setFormat`/`setTextureData`/`update`) as a controller property
  next to the geometries; in QML `Texture { textureData: path.stockField }` plus a `TextureInput` in a
  `CustomMaterial` (a property of type `TextureInput` becomes a `sampler2D` of the same name); the vertex
  shader reads the height with `texture(stockField, UV0)` and lowers the vertex, while the normal and the
  shading are computed in the FRAGMENT from the same bilinear reconstruction of the field — the CPU computes
  no normals at all. This is the documented Qt Quick 3D height-map technique.
- The grid is built once per load (position 3f + texcoord 2f, the same 20 B stride as `ToolpathGeometry`), and
  only the texture changes. The update replaces the whole buffer (there is no partial upload in
  `QQuick3DTextureData`), so the cadence is ours: while the program plays the field is published every 100 ms
  — 242 KiB per tick at a 0.5 mm node and 1.44 MiB at 0.2 mm on the 150x100 stock, otherwise megabytes per
  frame would go over the bus.
- The stock view is one switch with four positions (`stockView`): exact (one vertex per field node, as
  computed), smooth (the default: the mesh is subdivided, the height is bilinear), rounded (the node depth
  is the average over the neighbourhood) and exact contour (a smooth top plus the exact cut layers). The `R`
  key cycles them, `CNC_STOCK_VIEW` sets the view before the start, the former `CNC_STOCK_ROUND=1` remains a
  shortcut for `rounded`. Every view except the exact one is subdivided: `stockSubdiv = 2`, so a vertex also
  lands in the middle of a cell, where the side of the step is decided; the exact view is not subdivided — it
  is the very display of the node steps. The price is named outright: the drawing diverges from the field by
  up to a quarter of the cell twist, the cut edge shifts by up to half a cell (0.25 mm at a 0.5 mm node), and
  the mesh is four times heavier — 241 001 vertices and 480 000 triangles against 60 501 and 120 000. The
  field, its bytes and its digests do not change (the `stocksurface` case compares the buffer at `subdiv = 1`
  bit for bit).
- The layered surface (the plan, stages 1.1 and 1.5). The field top is a continuous bilinear height: the normal
  and the shading are computed in the fragment from the four nodes of the cell (a difference of neighbours one
  cell apart; a step inside a cell takes the analytic gradient of its own cell), and the `NORMAL_SPAN`
  compromise — a difference across 2 cells, by which the vertex rule caught a pocket wall — is gone. Measured:
  the smooth view differs from the former vertex rule on 1628 pixels out of 614 400 (0.27 %), the difference
  sits on the walls; a two-cell rise gets its own 1.0 mm/mm slope instead of the former 0.5
  (`stocksurface_shade_frag`), and there is no one-cell band at the edge (`stocksurface_fine`, 2489 checks,
  deviation 5.3e-15).
- The exact cut boundary is a representation OF ITS OWN, independent of the field node (the plan, stage 1.5).
  The core computes it from the moves (`core/stock_sim`, `stockCutBoundary`): `marching squares` over the signs
  of the field nodes picks the candidate edges, and the crossing position on an edge is refined by bisecting the
  segment over the EXACT analytic move depth (`sweepPointDepth` — the same formula as a field node). The sign at
  a node is exact (the field depth at a node is exact), so a node only picks the edge, not the boundary position.
  The wall (`stockCutWallFromSegments`) and the polyline (`StockCutContourGeometry`) are built from it, and for
  the colour and the line the core hands out the signed distance to the boundary at the field nodes
  (`stockCutSignedDistance`, negative where cut): the zero level between nodes is linear, so the edge runs as a
  straight line rather than in node steps. The surface fragment takes that value in the exact contour view
  (`cutExactBoundary`, `stocksurface.frag`); in the other views the colour and the line are as before, by
  `vDepth - cutMm`. The line colour is `CNC_STOCK_CONTOUR`, its thickness (1.1 px) is `stockContourHalfPx`.
  Boundary error in mm against the analytic case (`stock_sim_probe`, cases `boundary_wall` and `boundary_ball`):
  the former field threshold — 0.475 mm (wall) and 0.223 mm (ball) at a 0.5 node, and 0.190 / 0.044 at 0.2; a
  finer field (0.1 mm) — 0.095 / 0.0073; the exact boundary — 1.5e-8 / 6.7e-9 mm, and the drawn signed distance —
  0.011 / 0.002 mm. The price is named (Test_model): a 0.5 node — 2356 segments, 160 ms and 37 KiB, the signed
  distance 236 KiB (the same as the field); a 0.2 node — 5824 segments, 302 ms and 91 KiB, 1.44 MiB. A live tick
  recomputes only the cells whose nodes changed (the `StockBoundaryCache` state): 1.1-2.4 ms per step of
  0.1-0.5 % of the path length.
- The limits of the layered surface are named, not hidden. The cut wall is a seam `cutMm` tall (0.05 mm; about
  0.1 pixel on a 1024x600 frame at a 341 mm distance) on the plane of the untouched top, so the boundary line
  reads it, not the wall itself. The field top between nodes is still a height map (bilinear), while the colour
  and the line boundary follow the exact representation: the former caveat "node steps are cured by a finer node"
  is closed by a representation of its own, not by subdividing the field. The sagitta of the exact polyline is a
  chord between two sub-cell crossings (0.010 mm at a 0.5 node and radius 3), not an arc; the field node still
  decides the metal colour away from the boundary.
- The top of the stock wall is trimmed by the field (`stockshell.frag`): the wall stands on the bounds and
  otherwise would not know that the cutter removed metal across the edge (measured: 102 cut perimeter nodes out
  of 1000, up to 1.00 mm). The trim runs in the fragment shader and takes the same bilinear reconstruction of
  the field as the drawn top (the wall is one rectangle per face, and lowering only the corners would give a
  straight line between the corners). The shell buffer and its comparison (`stockshell`) are untouched; the
  gap at the edge is closed: the walls stood 1.5 px OUTSIDE the field bounds (`shellGapSideMm`), a gap of
  0.64 mm at dist=220 and 0.98 mm at dist=341, now `shellGapSideMm = 0` — the top and the wall meet in one
  edge.
- The stock stops being `#Cube`: the field draws the top face, the side walls and the bottom become a geometry
  of their own from five rectangles without the top face (the core `core/stock_sim`, `stockShellVertices`)
  with the old micro-offset against z-fighting. The box top and the untouched field nodes lie in the same
  plane, and the "1.5 px" offset is not allowed for the field: it would lie about the geometry. The walls are
  a solid body (opaque, writing depth) with their own normal from the vertex position (`stockshell.vert`) and
  a reversed cull side (`FrontFaceCulling`): the winding of the buffer is compared by the `stockshell` case,
  and with the usual cull side the NEAR faces were culled — measured 13 990 pixels over the wall; return
  `BackFaceCulling` if the winding is ever fixed in the core. Two materials, not one: the field top has its
  own palette and its own edge trim, the walls have the normal and the stock lamp.
- The stock is glass by default (`stockGhost`, density `stockGhostAlpha` 0.90): the pocket, the part and the
  tool are visible through the billet, and the cut metal reads by colour. These are the same two materials
  with the blending switched at run time (Blend and the depth write `AlwaysDepthDraw`), not a second model:
  without the depth write the far faces and the reference skin would go over the billet. The density 0.90 is
  the share of the material colour in a pixel: measured on a 1024x600 frame (Test_model, skin and lines off) —
  the copper cut `#BD5215` under the glass reads as `#DA7A44`, the untouched ochre `#BEA05C` as `#D3B87C`,
  while at 0.62 the same points almost dissolved into the background (`#FFCFAA` and `#F2DFB5`). The price is
  named outright: under the glass the cut and the untouched metal differ less than in the solid view, and the
  cut depth is harder to measure from the picture. The solid view is the same `V` key and
  `CNC_STOCK_GHOST=0` before the start.
- There is one switch: `showStockVisible` (the S key, the "Габарит заготовки" button) shows both the box and
  the field. There is no separate flag: a pile of toggles is already named as debt (§12). The outline and
  silhouette ribbons are untouched — they already run the depth test.
- The toolpath tail no longer fades: the fade band (`fadeBandT`) belongs to the "limit length" mode, and when
  the whole code is shown there is nothing to fade. The former `fadeStartT = 0.9` dimmed the last 10 % of the
  path even with a whole visible window, and on the billet that read as pale bands over the pocket (the tail —
  drilling — lies there densely); now `fadeStartT` is 1.0 while the window is whole. The palette and the hint
  were right, and the "blue pocket" is the toolpath lines, which run "always forward" (taken off by the `G`
  key, `CNC_NO_TOOLPATH`).
- The cut surface writes depth and therefore hides the path under the material; in the solid view that is the
  point of the simulation, but the familiar picture changes (the box is deliberately `NeverDepthDraw`). The
  default is glass, and the path is visible through it; taking the glass off (`V`, `CNC_STOCK_GHOST=0`) is the
  view for reading the cut depth from the picture.

**What the simulation must not do.**

- It must not slow the orbit: no per-frame work, no per-frame texture upload, no JS loop over hundreds of
  thousands of vertices. But the grid is triangles in the frame, and its cap above is part of the orbit budget
  (`PORT-CPP.md` §11: the orbit is cheap, the time goes into the cold load and heavy features).
- It must not touch the bit-exact contract: the field is in neither `PathData`, nor `PackedStream`
  (`cpp/core/gv_types.h`), nor the geometry buffers, nor `oracle_manifest.json`; `gv::prepareOrdered`, the parse
  and the packing stay as they are. The field accumulates float minima, so sha256 is no use for it at all; the
  comparison uses a millimetre tolerance (`stock_sim_probe`), and that exception to the general rule is named,
  not implied.
- It must not require an interpreter: the computation is in the C++ core (`cpp/core/stock_sim`) and QML draws
  it. The cache and the reference checks never see the field.

**Checks and what to measure.**

- These stay green: `gcode_probe --manifest` (the path and the prepared arrays), `step_probe --manifest` (the
  normals), `geometry_probe --manifest` (the six geometry buffers, their `stride` is frozen), the offscreen
  `loaded OK` and `RESULT: OK`. The seventh geometry is not part of the six-way comparison set.
- The check of the same layer is `stock_sim_probe` (analysis of the field and of its drawing, not a sha256).
  Cases: (1) a flat cutter over a flat top — a constant depth wherever the tool passed; (2) a ball cutter — a
  round valley (a true sphere); (3) a cone — a conical valley; (4) a cut above the stock — the field does not
  change; (5) a column caught only by a half-node step; (6) `stock_bind` — the field bound to the toolpath and
  reproducible bytes; (7) `stocksurface` — the top mesh of the field against the analysis of a flat plunge (the
  buffer at `subdiv = 1` bit for bit); (8) `stocksurface_fine` — the subdivision of the smooth view (2489
  checks, deviation 5.3e-15); (9) `stockshell` — the box shell without the top face; (10) `stockshell_trim` —
  the trim of the wall top by the field (33 cut nodes out of 41, the corners untouched); (11)
  `stockshell_trim_interp` — the bilinear reconstruction of the edge (the nearest node deviates by 1.00 mm, the
  bilinear sample by 0); (12) `holder_rings` — the holder rings with a 12° threshold (7 rings and a dip at
  44 mm before the fix, 8 rings after); (13) `stock_undercut` — a synthetic undercut profile and the `LOLLIPOP`
  type: the walk refuses, the field is not created (0 nodes, the stage and the tool number are named);
  (14) `stock_live` — the field accumulated by 10 Hz ticks matches the full pass bit for bit by digest, and the
  field after a rewind matches the pass from the start; (15) `stocksurface_exact` — the exact cut layers: the
  marching-squares contour ends against the analytic boundary of a ball (0.2227 mm against a half-cell tolerance
  of 0.25) and the wall whose top is on the plane of the untouched top and whose bottom is on the cut floor;
  (16) `stocksurface_shade_frag` — the slope in the fragment: a two-cell rise gets its own 1.0 mm/mm, while the
  former rule across 2 * NORMAL_SPAN cells gave 0.5; (17) `boundary_wall` and `boundary_ball` — the exact-boundary
  error in mm against the analytic case (1.5e-8 and 6.7e-9 mm against a 1e-3 tolerance) at the 0.5 and 0.2 nodes,
  with the same numbers for the former field threshold and a finer field printed alongside; (18) `boundary_drawn` —
  the zero level of the signed distance (what is seen) within 0.05 mm; (19) `boundary_incremental` — the boundary
  with state matches the full build segment for segment after the field grows. The tolerance is derived from the
  node size, not tuned until green; the digest cases compare bitwise.
- `stock_bind_probe` checks the same in the controller: the texture and the field match; during a tick (state
  `computing`) the field stays the last complete one; after a rewind and a move forward the digest equals the
  full field; cancelling a tick publishes not a new field but the last complete one, cancelling the first pass
  publishes nothing; `TYPE=LOLLIPOP` gives the state `refused` with no field; the cut-layer geometries are built
  from the exact boundary (20 segments of a synthetic boundary, a 40-triangle wall; the field is needed only for
  the wall floor).
- `front_stock_probe` holds the cut chronicle (`core/stock_front`): the field at moment t is a threshold on the
  time label and matches the walk from the start (digest); the cost of a rewind against a rebuild (Test_model:
  0.1442 ms against 81.920 ms, 568x); the determinism of two runs; the share of nodes cut more than once
  (38 825 cut nodes, 34 054 repeated — 87.7 %), the boundary of "the field at moment t is exact", named as a
  number. Exact playback needs a contribution journal (the plan, stage 2.4).
- Measurements: `bench_probe` — the phases next to `collision_scan`; the cost of the field — the `phase
  stock_sim_*` and `phase stock_live_*` lines of `stock_sim_probe` itself (0.5 and 0.2 mm nodes); the frame with
  and without the field — the `frame` lines of `bench_probe`, on weak hardware mandatory. The live numbers to
  watch: the collision scan 2.1 ms at a 1 mm step (i5-10400F), the NC parse 174 ms and `ngc_parse` 69.7 ms
  (i5-13420H), the cold load about 690 ms, the mesh silhouette 0.0791-0.0868 ms, the skin 45398 triangles
  (`med`), the full field pass 430 ms at a 0.5 node and 4.89 s at 0.2, the mean tick 0.0074 ms, a half-path
  rewind 200 ms.
- Measured: the 0.5 mm node (192.5 ms, 301x201), the 0.2 mm one (2288.6 ms, 751x501) and the `safe` 1.0 mm
  one (39.5 ms, 151x101) on Test_model. Proposals, not measurements: the caps 256/512/1024, the step = node/2,
  the R32F format, `safe` not drawing the field at all, the 628 KiB and 3.82 MiB of memory, the cap triangles
  130 000/522 000/2 093 000. The number of surface pixels is printed by `--frame` (`non-background pixels`) —
  once the surface was drawn that number moved, and the move is recorded deliberately rather than silently
  re-approved.
- Not taken before a prototype: whether the ION samples an R32F texture in the vertex stage on the OpenGL 3.3
  branch; what a whole-texture `update()` costs on a narrow bus; what an opaque surface does to path visibility
  (a screenshot); whether to draw the whole grid or only the touched rectangle (the latter is cheaper in
  triangles but needs a joint with the box top face); whether R16F is needed. The prototype is a measurement
  only; it does not enter the tree.

## 14. Checks and tests

Before committing — the full list is in VERSIONING.md, section 5 (the build `scripts/build-viewer.ps1`, the six
probes with `--manifest`, `version_probe`, `stock_sim_probe`, `front_stock_probe`, `stock_bind_probe`, `--smoke`, the offscreen run,
`keyprobe`, `--frame`).

Performance — `bench_probe` (cold-load phases and frames) and `stock_sim_probe` (the cost of the stock field).
Numbers enter a comment only when they are measured.

## 15. Directions for panel development on the 3D base

Five panel features built on the viewer scene and data (the panel is `apps/react-interface/lib/cnc`). For each: the
pain today, what the operator does, and what the code rests on — and what is still missing.

1. **Interactive 3D probing (click a face).**
   - Pain: the probing macro is picked from a picture and the sizes are typed by hand; a typo in the approach breaks a
     ruby stylus.
   - Operator: clicks a face of the loaded STEP part; the base recognises the geometry (a hole's cylindrical wall ->
     the diameter, two parallel faces -> a boss width), picks the matching probing macro, fills the approach and shows
     the proposed probe path in 3D before Cycle Start.
   - Rests on: the analytic faces are already extracted — `cpp/occt/step_import.cpp` (`gv::loadStepModel`, tessellation,
     `GeomAbs_Plane`), `cpp/occt/step_surfaces.cpp` (`gv::loadStepSurfaces`, `GeomAbs_Cylinder`/`GeomAbs_Cone`), the
     `CylinderSurface`/`ConeSurface` types in `cpp/core/gv_types.h`, the C++ mirror `cpp/occt/step_surfaces.cpp`; the probing
     macros — `PROBE_CYCLES`/`computeProbeResult` (`lib/cnc/probe.ts`). Required: a ray face pick in
     `GcodeViewer3D.qml` (today `MouseArea` is orbit and pan, there is no ray), mapping a face to a macro, and drawing
     the proposed probe path.

2. **"Time machine" and safe restart.**
   - Pain: "run from line" after a broken cutter is a gamble.
   - Operator: scrubs the 3D trajectory to the break point; the base reads the context (active tool, spindle rpm, work
     offset), builds a safe approach tail (raise Z to machine zero -> spindle and coolant -> rapid X/Y above the start
     point -> descend to clearance -> continue) and draws the tail in 3D before the start.
   - Rests on: the path data and stages exist — `PathData`/`Stage` (`cpp/core/gv_types.h`), `sampleAt`/`stages`
     (`cpp/qmlbackend/gv_toolpath.cpp`), `setToolStage`/`stageTools`, the stick-out `gv::toolExposedLength` (`cpp/core/tooling.cpp`); the
     background pass and token — `gv::scanCollisions`/`collisionToken` (`cpp/core/collision.cpp`, `cpp/qmlbackend/gv_toolpath.cpp`), the stock
     field — `cpp/core/stock_sim.{h,cpp}`. Required: the viewer parses neither the `S` speed nor the work offset
     `G54`..`G59` (`cpp/core/gcode_parser.cpp` knows motion and planes only), and the generation and drawing of the approach tail
     are missing too.

3. **Selective machining (a 3D window).**
   - Pain: otherwise a 40-minute program is run in full, or a CAM programmer is asked to cut out a fragment.
   - Operator: holds Shift and drags a 3D box around the problem area; the base filters the trajectory and keeps the
     program lines that intersect the volume — only that corner is re-machined.
   - Rests on: the path geometry and bounds exist — `PathData.pts`/`cum`, `Aabb` (`cpp/core/gv_types.h`), the travelled
     distance `pathDist` (`GcodeViewer3D.qml`). Required: selecting a volume in the scene and a filter by volume, and
     the link from a segment to a program line must be built from scratch — the viewer keeps no line numbers, and
     "linking 3D and the NC line" is listed as not done in §13.

4. **Trajectory heat map (load).**
   - Pain: on chatter the operator turns the feed override blindly.
   - Operator: after the first part sees the trajectory coloured by load (green — normal, yellow — rising, red — peak
     and chatter) from the spindle load and the actual feed, and edits the program.
   - Rests on: the trajectory and the programmed feed exist — `toolpath.vert`/`frag` already colours fragments by
     `cutoff`/`pathDist`, `speedAt` (`cpp/qmlbackend/gv_toolpath.cpp`). Required: no layer reads the live machine state;
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
     (`gv::loadStepModel`, `cpp/occt/step_import.cpp`), the collision scan — `gv::scanCollisions`/`gv::holderProfile`
     (`cpp/core/collision.cpp`, `cpp/core/tooling.cpp`). Required: a slot for a second STEP model (the viewer has one today,
     `modelPath` in `cpp/qmlbackend/gv_toolpath.cpp`), snapping the fixture model to the touch, and a "holder against fixture"
     scan: today `gv::scanCollisions` compares the holder cylinders with the stock bounds, not with the fixture body.

7. **The "ghost" of the future (look-ahead projection).**
   - Pain: on a first part the operator holds the e-stop and stares through the coolant — the next move is not
     visible.
   - Operator: sees a semi-transparent ghost tool running along the trajectory 3-5 s ahead of the real one, so
     the next manoeuvre is visible before the machine makes it in iron.
   - Rests on: the whole program is parsed ahead (`gv::parseGcode` -> `gv::prepareOrdered`, `cpp/core/path_prep.cpp`), the path
     is fully in the samples (`pathSamples`/`cum`), the position comes from `sampleAt`/`speedAt` (`cpp/qmlbackend/gv_toolpath.cpp`),
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
      `StockField`/`gv::simulateStock` (`cpp/core/stock_sim.{h,cpp}`), the tool of a stage — `stageTools`/`Stage`
      (`cpp/qmlbackend/gv_toolpath.cpp`, `cpp/core/gv_types.h`). Required: volume from the height field per stage (or OCCT booleans) and
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
      fields — `gv::toolProfile` (`cpp/core/tooling.cpp`: FLAT, BALL, BULL, CHAMFER, TAPER, DRILL, TAP, LOLLIPOP, DOVETAIL, PROBE),
      the fields are parsed by `parse_ui_tool` (`cpp/core/ui_tags.cpp`). Required: code input (QR/RFID), a tool database
      and turning its record into `UI_TOOL` fields; there is neither a scanner nor a database in the tree.

13. **Kinematically accurate time prediction.**
    - Pain: CAM cannot know the machine's accelerations and path blending (`MAX_ACCELERATION`, `G64`/arc
      blending), so its estimate never matches the stopwatch.
    - Operator: the station runs the program through the controller's planner in the background, gives a cycle
      time that matches the stopwatch, and recomputes it when the operator turns the physical feed override.
    - Rests on: the panel already shows a remaining estimate — `remainingLength` (`toolpath.ts`) and `restMs` in
      `program-screen.tsx` divide the remaining path by `programFeed` (`machine.ts`), while the `feedOverride`
      correction acts on the current move only (`motionSpeed`, `coords.ts`); the parser reads the modal `F`
      (`feed`/`_to_mms`, `speedAt` — `cpp/core/gcode_parser.cpp`, `cpp/qmlbackend/gv_toolpath.cpp`).
      Required: a planning pass over accelerations and blending; no layer reads them (`cpp/core/gcode_parser.cpp` parses
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
