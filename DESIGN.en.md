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

- `preview_3d.py` — the main launch. Creates `QGuiApplication`, registers the QML module `MockBackend 1.0` by
  importing `mock_backend`, loads `preview_3d.qml`; with an argument it calls `load_gcode_async`. In offscreen
  mode it takes the synchronous path and prints `loaded OK`.
- `preview_3d.qml` — a 1024x600 `Window` holding `GcodeViewer3D`.
- `preview_qml.py` — another launch point (the Qt interface). This duplication is listed in section 12.

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
- Several launch points (`preview_3d.py`, `preview_qml.py`) create ambiguity.

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
- the live frame checks (`test_solid_skin.py`, `test_ribbon_width.py`) are red on the development machine
  and on a clean `HEAD` (11792 px against a threshold of 10500; 0.00 px against 2.0) — they were measured on
  another machine and need a hardware GL; the guard does not run them;
- `tools/check.ps1` (`pnpm guard`) does not run `bench_viewer.py` — performance regressions are not caught;
- `apps/qt-interface/main.qml` instantiates a type `Toolpath` that no backend provides (the class is named
  `GcodeToolpath`) — the file does not load regardless of the port; `preview.qml` does load.

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
  2D texture of the Z-buffer that a vertex shader extrudes into plane polygons;
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
