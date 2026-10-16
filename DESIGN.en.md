# DESIGN.md — 3D viewer for NC programs (`gcode-viewer`)

The source of truth is the code and its behavior (CLAUDE.md), not this description: the document explains the
design and may lag behind. Change this file together with behavior. Russian version — [DESIGN.md](DESIGN.md);
edit both languages in the same pass.

## 1. Purpose and place in the product

`gcode-viewer` is the third of the product's four projects (manifest — [projects.json](projects.json)):
interactive 3D preview of a machine control program. Directory — `apps/qt-interface`. Stack: QML + QtQuick3D +
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
- `preview_qml.py`, `preview_pyqt5.py` — other launch points (the Qt interface and an older variant). This
  duplication is listed in section 12.

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
- `mock_backend.py` is one monolith (about 4300 lines): parser, STEP/STL, analytics, silhouette, collisions,
  tool check, and all geometries. It should be split into modules (`gcode_parser`, `step_import`, `tangents`,
  `collision`, `geometry`, `controller`).
- `GcodeViewer3D.qml` (about 1000 lines) holds JS camera, projection and stage logic; it belongs in backend
  slots.
- Several launch points (`preview_3d.py`, `preview_qml.py`, `preview_pyqt5.py`) create ambiguity.

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
- `test_solid_skin.py` is red on the development machine (11792 px against a threshold of 10500, and on a
  clean `HEAD` as well); the threshold was measured on another machine, and the guard does not run the test;
- `tools/check.ps1` (`pnpm guard`) does not run `bench_viewer.py` — performance regressions are not caught.

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
  GL context is required, not a software rasterizer.

## 13. Directions for improvement

Functionality:
- material removal simulation (heightmap/dexel, in the background on a weak CPU);
- gouge check of the tool against the part model;
- machining time and ETA (`moveLength`/`speeds` already exist);
- linking 3D and the NC line (click a path point -> block/line, search);
- section planes, measurements, point coordinates;
- work offsets G54..G59, reference point;
- STEP holder models (B-rep import already exists; needs an anchor/gauge, a model catalog, a cache, and
  collision primitives from the analytic cylinders instead of the hard-coded ER20);
- FPS meter and quality presets (low/med/high), a dark theme, program drag-and-drop.

Engineering:
- split the monolith and introduce a single `dataclass` contract QML <-> Python;
- `dataclass` instead of tuples/dicts, with type hints: module-boundary structures are in place
  (`gv_types.py`, 0.13.13); core type annotations remain;
- a performance gate in `pnpm guard` via `bench_viewer.py`;
- centralized logging;
- port the compute core to C++ (the OCCT SDK directly instead of bindings, Eigen instead of
  numpy) — estimate, risks and stages in [`PORT-CPP.md`](PORT-CPP.md) /
  [`PORT-CPP.en.md`](PORT-CPP.en.md).

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
