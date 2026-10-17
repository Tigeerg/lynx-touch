# Plan: porting the `gcode-viewer` backend to C++

An estimate and an order of work. This document describes a **plan**, not a decision: it exists so
that each stage can be chosen deliberately and its price is visible.

The reason for the port: C++ is stricter about types and allows much more serious optimisation.
Below: where that optimisation pays off, where it does not, and what it costs.

State at the time of the estimate: viewer **0.13.4**, `mock_backend.py` is 4092 lines,
`GcodeViewer3D.qml` is about 1050 lines, plus `cpp/stepledgesgeometry.{h,cpp}` (the `CNC.Edges`
plugin, not buildable: the system has no Qt6 dev packages).

---

## 1. Inventory of the Python code by layer

| # | Layer | Key entities | Dependencies |
|---|-------|--------------|--------------|
| 1 | Common plumbing | `_occt` lock, numpy import, `CNC_*` environment constants | threading, numpy |
| 2 | G-code parser | `parse_gcode`, `_run_cycle` (drilling), `_arc_points`, `_arc_sweep` | re, math |
| 3 | `UI_*` tags | `parse_ui_rotation/origin/tool/holder/stock`, `stage_label`, `quat_from_cols` | re, math, QQuaternion |
| 4 | STL import | `load_stl_vertices` (binary and ASCII), `_verts_bounds` | struct, numpy |
| 5 | Edge packing | `_pack_edges_np`, `packed_positions`, `_pack_edge_segments`, `_coalesce_segments` | numpy |
| 6 | STEP import | `_step_shape`, `load_step_model`, seam filter, `_orient_shell_outward` | OCP (STEPControl, BRepMesh, TopExp, BRepAdaptor, GCPnts) |
| 7 | Analytic surfaces | `load_step_surfaces` (cylinders and cones: axis, radius, arc) | OCP (BRepAdaptor_Surface, gp, GeomAbs) |
| 8 | Crease-aware normals | `_smooth_face_normals` (grid welding, batching by group size, 20 degree clustering) | numpy |
| 9 | Silhouette topology | `_build_silhouette_topology` (weld 1e-4 mm), `_mesh_silhouette_segments` (sign of `n·E - d`, int8) | numpy |
| 10 | Analytic tangents | `_tangent_families` (precomputed), `_tangent_from_families` (per frame), `_cone_keeps_silhouette`, `_dedupe_cones` | numpy, math |
| 11 | STL outline | `_feature_edges` (dihedral angle), `_feature_edges_np` | numpy |
| 12 | Tool path preparation | `_prepare_ordered` (the core), `_pack_stream_np`, `_stages_norm`, `_scene_bounds_lo_hi` | numpy |
| 13 | Tool and holder | `_tool_profile`, `_tool_outline_profile`, `_holder_*_profile`, `_rings_segments` | math, array |
| 14 | Collisions and tool check | `_scan_collisions`, `_circle_rect_hit`, `_tool_check_report` | math |
| 15 | Disk cache | `_cache_key`, `.npz` read/write, `_cache_prune` | hashlib, json, numpy |
| 16 | Qt geometries | `ToolpathGeometry` (stride 20), `HLRLineGeometry` (stride 48), `GridGeometry`, `AxisArrowGeometry`, `MeshGeometry`, `ToolGeometry` | Qt Quick3D, numpy, array |
| 17 | Controller | `GcodeToolpath`: properties, slots, signals, `refreshTangents`, `_load_model_verts`, `_apply_mesh_verts`, `_compute_edges`, `load_gcode*`, background threads | Qt Core/Gui/Qml/Quick3D, threading |

Order of magnitude: **about 4.1 thousand lines of Python**, of which roughly 2.6 thousand are the
compute core, 1.0 thousand the controller and 0.5 thousand the Qt geometries.

Key dependencies: **numpy**, **OCP** (`cadquery-ocp`) for STEP only, **PySide6**,
and `threading` with a lock around OCCT.

## 2. The QML contract that must not break

The port must keep the QML contract, or the whole viewer stops working.

- **Data properties:** `center`, `radius`, `moveLength`, `stages`, `collisions`, `toolCheck`,
  `stockCenter`, `stockSize`, `hasStock`, `holderGauge`, `toolInfo`, `toolLength`, `toolRadius`,
  `holderRadius`, `modelPath`, `partMeshCenter`, `partRadius`, `partRotation`, `partPosition`,
  `toolPosition`, `loading`, `eyeRaw`.
- **Geometry properties:** `rapidGeometry`, `feedGeometry`, `partGeometry`, `partEdges`, `hlrEdges`,
  `hlrTangents`, `toolGeometry`, `toolOutline`, `holderGeometry`, `holderOutline`, `toolRings`,
  `holderRings`.
- **Signals:** `ready`, `pathChanged`, `modelPathChanged`, `meshChanged`, `partTransformChanged`,
  `stockChanged`, `toolChanged`, `loadingChanged`, `toolInfoChanged`, `collisionsChanged`,
  `toolCheckChanged`, `loadFinished`, `eyeChanged`, `toolpathLoaded(int)`, `toolpathReady(object)`,
  `edgesReady(object)`, `meshReady(object)`.
- **Slots:** `setToolStage`, `analyzeCollisions`, `setMachineTools`, `setMachineLimits`,
  `runToolCheck`, `refreshTangents`, `load_gcode`, `loadModel`, `load_gcode_async`, `load_string`,
  `sampleAt`, `isRapidAt`, `speedAt`.
- **Shader buffer formats** move 1:1: `HLRLineGeometry` — stride 48 (POSITION vec4, NORMAL vec4 =
  segment direction, TEXCOORD0 vec4 = side; `stepledges.vert` reads `UV0.x`, `NORMAL` and the
  uniforms `eyeRaw`, `camFloatMm`, `lineWidthPx`, `viewportW/H`, `polyOffset`); `ToolpathGeometry` —
  stride 20 (POSITION 3f + TEXCOORD0.x = path parameter; `toolpath.vert/.frag` handle `cutoff`,
  `pathEnd`, `fadeStart`, `stageMode`, `stageStart`, `stageEnd`).
- QML uses `GridGeometry` and `AxisArrowGeometry` directly, not through the controller.

## 3. What is already C++-friendly

- **Qt geometries** are thin wrappers over `QQuick3DGeometry`; the call order
  `clear -> setVertexData -> setStride -> addAttribute -> setBounds -> update` already exists, and
  Python only prepares bytes.
- **The parser core and `_prepare_ordered`** are Qt-free and thread-safe: they translate into free
  functions.
- **OCCT calls** are already listed in `cpp/stepledgesgeometry.cpp`; the same API exists in the C++
  SDK.
- **Load tokens** (`_load_token`, `_edge_token`, `_collision_token`) map onto `std::atomic`.
- **`set_packed`**: the buffer is built off the GUI thread and only installed — same semantics.

## 4. What is replaced, and what that buys

| Now | In C++ | Gain |
|---|---|---|
| OCCT calls through `OCP.*` SIP bindings | OCCT C++ SDK directly | **The main one**: in `load_step_model` and `load_step_surfaces` every vertex, triangle and point goes through Python↔C++ marshalling (`tri.Node(i)`, `tri.Triangle(i)`, `sampler.Value(i)` — about 180 thousand exchanges on Test_model). It disappears |
| numpy for `cross`, `norm`, `einsum` | C++ loops with an explicit summation order | No intermediate ndarray objects; the 36 optional-numpy branches go away. Eigen is not used — see §11 |
| `threading.Thread` and the lock around OCCT | `QThreadPool`, `QtConcurrent` | No GIL; today OCCT releases the GIL on purpose, so sections are serialised by a lock |
| Python loops while building buffers (`ToolGeometry`, `GridGeometry`, ASCII STL) | C++ loops and vectors | The interpreter leaves the hot buffer-building paths |

Measured baseline (`bench_viewer.py`, Test_model, weak CPU): `step.shape_read` 858 ms,
`step.read` 571 ms, `gcode.parse` 385 ms, `stl.feature_edges` 357 ms, `step.surfaces` 68 ms,
`families.build` 37 ms. The interactive path is already within budget: 0.71 ms per camera event, and
the idle frame is limited by the GPU.

**Conclusion:** the gain is concentrated in cold loading and in future heavy features (material
removal, gouge check), not in the orbit. A port for the orbit alone is not worth it.

## 5. What is not worth porting

1. **Python fallbacks without numpy** (`_pack_stream_py`, `_feature_edges_py`, the Python branches of
   `_pack_edge_segments` and `MeshGeometry.set_triangles`) **are already removed** together with the
   hard dependency: there is nothing left to port.
2. **`GridGeometry` and `AxisArrowGeometry`** — static procedural geometry, small buffers, rebuilt
   rarely: the gain is about zero.
3. **The disk cache** in `.npz` format — moving the format breaks compatibility with the Python cache.
   Either keep it as a sidecar, or deliberately move to `QDataStream` and raise `CACHE_VER`.
4. **Render acceptance tools** (`test_solid_skin.py`, `test_ribbon_width.py`, `render_probe.py`,
   `smoke_test_qml.py`) — they test the whole chain and must keep working against the new backend
   with the QML contract unchanged.
5. **The camera and stage JS logic in QML** — it belongs in the backend, but that is a separate
   refactoring; it must not be mixed with the port.
6. **`_tool_check_report`** — a skeleton with no controller data source: cheap to port, no value
   until the contract is defined.

## 6. Risks and cost

- **Size:** expect ×1.5–1.8 in lines (headers, explicit types, templates, CMake).
- **Build time:** OCCT headers are heavy; one TU per layer, a PCH for OCCT and Qt.
- **OCCT licence:** LGPL-2.1 **with the OCCT exception** — dynamic linking to `TK*.dll` is safe for
  a closed product. **Static linking is a grey area** and is not recommended. `cpp/CMakeLists.txt`
  is already ready for `find_package(OpenCASCADE)`; a full port also needs `TKMesh`, `TKPrim`,
  `TKG3d`.
- **Infrastructure:** there are no Qt6 dev packages on the machine, so the C++ part cannot be built
  at all until they are installed. Eigen is deliberately not used: `Eigen::cross()`/`.norm()` give a
  different summation order and FMA, while the golden checks demand bit-for-bit (see §11).
- **Cross-platform:** Qt and OCCT build on Windows and Linux alike; but the target weak
  hardware (Atom + Ion, legacy OpenGL 3.3) stays GPU-bound — the port does not change that.
- **The main risk is not size but exactness.** `test_normals_golden.py` demands a **bit-for-bit**
  float32 match and `test_tangent_policy.py` an element-wise silhouette match. The result depends on
  summation order, batching order and rounding. The key difference: `numpy.round` rounds half to
  even, `std::round` rounds half away from zero. This needs `/fp:precise` (MSVC),
  `-ffp-contract=off -fno-fast-math` (GCC/Clang) and a hand-reproduced summation order; otherwise
  the golden fails and the normals must be re-approved, which breaks an already accepted requirement.

## 7. Staged plan

The principle: **first the Python code is brought into a portable state and produces golden
artifacts (an oracle), then C++ replaces layer by layer as a drop-in with the QML contract
unchanged.** Every stage leaves the Python backend working; switching is a flag.

| Stage | What is done | Green after the stage | Rollback |
|---|---|---|---|
| **0** | Python only: split the monolith into modules, introduce types instead of tuples and dicts, make numpy mandatory, `print` -> `logging`, add the missing tests, **generate the oracle** (dump intermediate arrays on Test_model) | Existing checks and new tests; the oracle is written | Nothing to roll back: behaviour is unchanged |
| **1** | Build skeleton: CMake, Qt6 Quick3D, OCCT; make `cpp/stepledgesgeometry` buildable; wire the build into the guard | The plugin builds, its segments match `load_step_model` | The plugin is separate and breaks nothing |
| **2** | Pure core: G-code parser, cycles, arcs, `UI_*` tags, `_prepare_ordered`, stage packing | C++ gives identical buffers and stages against the oracle | The Python core stays |
| **3** | Qt geometries: `ToolpathGeometry`, `MeshGeometry`, `ToolGeometry`, `HLRLineGeometry` with the buffer formats | `smoke_test_qml`, `test_ribbon_width` green, buffers match | The Python geometries stay |
| **4** | **STEP, STL, normals, topology, silhouette** on the OCCT SDK directly | `test_normals_golden` bit-for-bit, `test_normals_orientation`, `test_tangent_policy` green, cold loading down several times over | The Python STEP branch stays as the diff reference |
| **5** | The `GcodeToolpath` controller: properties, slots, signals 1:1, async via `QThreadPool`, tokens via atomics | `smoke_test_qml`, `test_solid_skin`, `render_probe`, `preview_3d` (offscreen and live window), not one `[QML warning]` | Back to the Python type registration |
| **6** | Periphery: cache, `_scan_collisions`, `_tool_check_report`, STL outline, `GridGeometry` and `AxisArrowGeometry` | As a leftover | — |

**The oracle (stage 0) is ready.** `modules/gcode-viewer/oracle_dump.py` computes the intermediate arrays of
the Python path (STEP: mesh, edges, normals; silhouette topology; silhouette at five fixed eyes; STL: soup and
outline) and compares their `sha256` with the `oracle_manifest.json` manifest. Only the manifest is in the
repository - the arrays are tens of megabytes and are produced by running the script (`--write` rewrites the
manifest deliberately, together with the change that altered the result). The check runs in the guard: 22
entries, a mismatch means an intermediate result has changed.

**The monolith split (stage 0) is done as well.** `mock_backend.py` was 4620 lines and is now a 104-line
facade; the code is spread over 18 modules: `gv_types`, `gv_common`, `gcode_parser`, `ui_tags`, `cache_store`,
`stl_import`, `packing`, `step_import`, `step_surfaces`, `tangents`, `silhouette`, `normals`, `outline`,
`path_prep`, `tooling`, `collision`, `geometry`, `controller`. The names used by the tests,
`bench_viewer.py`, `oracle_dump.py` and QML are re-exported by the facade, so the outside code never
changed, and the oracle confirmed on every step that no result moved.

**Stage 0 is closed.** Types instead of tuples are in place (`gv_types.py`, 0.13.13 — path points, stages,
bounds, buffers, STEP surfaces, silhouette topology, tool and holder profiles), numpy became a hard
dependency (0.13.11), the library prints became logging (0.13.12), and the missing tests were added — the
G-code parser, drill cycles, tool profile and collisions (0.13.14).

**The build skeleton (stage 1) is in place.** `scripts/build-viewer.ps1` builds the `CNC.Edges` plugin with
the Ninja generator (MSVC, Qt 6.12 Quick3D, OCCT 8.0.1) into `build/edges`. The STEP edge segments
are checked against the Python reference `load_step_model` by the `cpp/stepledges_probe.cpp` probe and the
`test_stepledges_plugin.py` check: 2426 segments, the point set matches (sha256 `ff0d95ac…`), only the
traversal order differs — in the Python `_coalesce_segments` the clusters are ordered by an unstable
`numpy.argsort`. The build and the comparison sit in the guard and are skipped when the toolchain is absent.

**The clean core (stage 2) is in place.** The G-code parser, cycles, arcs, the `UI_*` tags,
`_prepare_ordered` and the vertex-stream packing are ported to C++ without Qt or OCCT (`cpp/core`, target
`gvcore`). The match is bit-exact: `path.points`, `path.flags`, `path.last`, `path.segments`, `path.stages`
produce the same `sha256`, and the vertex streams are equal byte for byte; the `cpp/gcode_probe.cpp` probe
and the `test_gcode_core.py` check do the comparison. The core is not wired into the backend yet: it is a
parallel implementation, switched by a flag in stage 3/5.

**The Qt geometries (stage 3) are in place.** All six `geometry.py` geometries are ported to
`cpp/qt/gv_geometry` (target `gvqtgeom`) with the same buffer formats, attributes and vertex order. The
`cpp/geometry_probe.cpp` probe and the `test_geometry_core.py` check compare the buffers with the Python
ones byte for byte — 14 cases match, including empty geometries and the float32 arithmetic of the ribbons.
The classes are not QML types yet and are not wired into the controller: switching the backend is stage 5.

**The STEP tessellation and skin normals (stage 4, part 1) are in place.** The STEP import is ported to the
OCCT SDK directly (`cpp/occt/step_import`, target `gvstep`), the skin normals live in `cpp/core/normals`.
The match is bit-exact: `step.verts` [136194,3], `step.tri` [45398,3,3] and `step.normals` [45398,3,3]
produce the same `sha256` as the reference and the oracle; the `cpp/step_probe.cpp` probe and
`test_step_core.py` do the comparison.

**STL and the outline (stage 4, part 2) are in place.** The STL import, the mesh outline and the edge
packing are ported to `cpp/core` (target `gvcore`), bit-exact: `stl.soup` [562776,3] and
`stl.feature_edges` [1435,2,3] produce the same `sha256` as the reference and the oracle; the
`cpp/stl_probe.cpp` probe and `test_stl_core.py` do the comparison.

**The silhouette topology (stage 4, part 3) is in place.** `silhouette.py` is ported to
`cpp/core/silhouette`; eleven oracle keys (`topo.n/d/f0/f1/ea/eb` and `silhouette.eye0..eye4`) match
bit-exact; the `cpp/silhouette_probe.cpp` probe and `test_silhouette_core.py` do the comparison.

**Stage 4 is closed.** `step_surfaces.py` is ported to `cpp/occt/step_surfaces`, `tangents.py` to
`cpp/core/tangents`; on Test_model and Test_model_2 all 22 dumps match (surfaces, families, cone policy,
the silhouette on five eyes) — the C++ `sha256` equals the Python reference. Digging into the mismatches
turned up two places in the reference: `** 0.5` there is `pow`, not `sqrt`, and the face-triangulation
branch is unreachable because the OCP `Poly_Triangulation` wrapper has no `IsNull`, so the points come
from the face vertices. The C++ port reproduces the observed behaviour.

**Stage 5 is the backend switch.** The C++ layers are checked against Python but not wired in yet: the
controller still runs on the Python geometries. Stage 5 replaces them and enables the C++ path by a flag.

**The bridge and the flag (stage 5, part 1) are in place.** The C++ layers were static libraries without a
C ABI, that is, unreachable from Python. A SHARED target `gvbridge` with a plain `extern "C"` API over the
Qt-free core (skin normals, the STEP model), a Python shim `gv_cpp.py` (ctypes) and a flag `CNC_CPP`
(`normals` | `step` | `all`, off by default, modelled on `CNC_SILHOUETTE`) were added. The STL normals and
the STEP branch are switched; a C++ failure logs a warning and falls back to Python. Byte-for-byte the same
with and without the flag; the QML contract is unchanged. Still to do in stage 5: move the surfaces, the
silhouette topology and the parser into the bridge, then the Qt geometries and the controller.

**The bridge and the flag were extended (stage 5, part 2).** `CNC_CPP` accepts `normals`, `step`,
`surfaces`, `silhouette`, `parser`, `all`. The STEP surfaces, the silhouette topology, the mesh silhouette,
the tangent families, the G-code parsing and the path preparation are switched — byte-for-byte the same as
Python (the oracle under the flag confirms it too). With `step`+`surfaces` the STEP file is read once:
before, Python read it a second time for the surfaces. Numbers on the development machine: mesh silhouette
per frame 0.56 -> 0.48 ms, tangents 0.60 -> 0.14 ms. Still to do in stage 5: the Qt geometries and the
controller — a separate step that must verify loading the Qt plugin into the PySide6 process.

**The Qt geometries are wired in (stage 5, part 3).** The QML type registration stayed in Python while
`gvgeom` builds the buffers through the `gvqtshim` shim; the flag value is `CNC_CPP=qtgeom`. The buffers of
all six geometries matched the Python ones inside the live PySide6 process. The key point was verified too:
`Qt6Core.dll` resolves by name to the PySide6 Qt, ordinary SHARED libraries with Qt objects load
unconditionally, and the QML plugin needs the module directory plus `OCCT/bin` in `PATH` before the engine is
created.

**The C++ controller is in place (stage 5, part 4a).** The `MockBackend` 1.0 plugin (`cpp/qmlbackend`)
provides the same geometry types and the C++ class `GcodeToolpath`: NC loading, buffers, properties, sampling
slots and signals work and match the Python path (70914 segments; buffers and `center/radius/moveLength/minY`
bit for bit). Async uses `QThreadPool` and an atomic token instead of `threading`. The model, the outline and
silhouette, the tool and holder, the stock, the collisions and `toolCheck` are stubs for now and log a
warning. It is enabled by `CNC_CPP=controller`; the Python registration is skipped in that mode, otherwise QML
does not load at all.

**The model and the silhouette are ported (stage 5, part 4b).** The C++ controller reads STEP (soup, normals,
edges and surfaces in one pass) or STL, publishes `partGeometry`/`partEdges`/`hlrEdges`/`hlrTangents`, the part
transform and the stock; the silhouette is computed both from the mesh (`CNC_SILHOUETTE=mesh`) and analytically.
The mesh and silhouette buffers matched the reference bit for bit, the outline matched as a point set (the order
differs for the already accepted reason). `render_probe` does not give parity yet: the difference is entirely in
the layers not ported in step 5.4c (tool, holder, rings, the collision scan). Still to do in stage 5: the tool
and holder, the stock, the collisions and `toolCheck`, the cache.

**The tool, the collisions and the tool check are ported (stage 5, part 4c; stage 5 is closed).**
`tooling.py` and `collision.py` moved into `cpp/core`; the controller publishes all six tool and holder
geometries, the stick-out and the radii, the collisions, `toolCheck` and `toolIssueCount`; the scan runs in the
background on a `QThreadPool`. Exactness required repeating the CPython functions: `vector_norm` instead of
`sqrt` (differs by 1 ulp in 8 pairs out of 51) and a half-to-even `round` (otherwise the sign differs at
12.345, 2.675, 0.005). The tool and holder buffers and the collision/`toolCheck` structures matched the
reference; `render_probe` converged (without the flag `edge_px 7905`, `stock_px 67235`; with the flag the same
numbers). What remains: the disk cache — a separate decision.

**Python is out of the viewer runtime.** The viewer no longer computes in Python: the entry points
(`preview_3d.py`, `smoke_test_qml.py`, `render_probe.py`, `test_solid_skin.py`) and the new C++ entry
point `cpp/viewer/main.cpp` (target `gcodeviewer`, `<repo>/build/viewer/gcodeviewer.exe`) always load
the `MockBackend` QML plugin and never import the Python backend. The `CNC_CPP` flag is gone from the
runtime: it remains only for the comparison checks (`test_cpp_bridge.py`, `test_controller_cpp.py`),
which need both paths in separate processes. Without the plugin the viewer fails with a clear message
naming `scripts/build-viewer.ps1` and a non-zero exit code; there is no silent fallback to Python. The
C++ entry point runs without an interpreter: it puts the plugin directory and the OCCT DLLs into `PATH`
itself before the engine is created, Qt must be in the process `PATH` (the exe links against
`Qt6Core.dll`), and the window comes from `QQuickView` over `GcodeViewer3D.qml`. The runs agree:
offscreen on `Test_model.ngc` - 70914 segments and `loaded OK`; a live window - the same 70914 and not a
single `[QML warning]`.

The Python core modules (`controller.py`, `geometry.py` and the rest) stay in the repository as a
development tool and the reference for comparisons - `oracle_dump.py`, `bench_viewer.py` and the
comparison `test_*.py` use them. The `.npz` disk cache is a development tool as well: the C++ controller
always re-reads the model, needs no cache, and the cache is not in the viewer runtime path.

**Stage 4 gives the bulk of the gain.** Stages 2 and 3 are cheap and safe; stage 5 is needed to let
go of Python altogether but barely speeds anything up on its own.

## 8. Estimates

Assumptions: a direct translation of the algorithms, scalar C++ with an explicit summation order instead of
numpy (Eigen is not used — see §11), OCCT linked
dynamically, the QML contract unchanged, bit-for-bit pursued for normals and silhouette.

| Layer | Complexity | C++ lines, roughly |
|---|---|---|
| CMake skeleton and plugin | low | 150–250 |
| G-code parser, cycles, arcs, tags | medium | 700–900 |
| Tool path and stage preparation | low-medium | 300–400 |
| Cache (if ported) | low-medium | 200–300 |
| STL | low | 120–180 |
| Edge packing and coalescing | medium | 200–300 |
| **STEP and normals** | **high** | **900–1300** |
| **Analytic surfaces and tangents** | **high** | **700–1000** |
| Silhouette topology | medium | 250–350 |
| STL outline | medium | 250–350 |
| Tool, holder, rings | low-medium | 350–500 |
| Collisions and tool check | low-medium | 300–400 |
| Qt geometries | low | 500–700 |
| Controller | medium | 800–1000 |
| **Total** | — | **about 5500–7500** |

The two high-risk layers (STEP with normals, and the analytic silhouette) are about 1.6–2.3 thousand
lines and almost all of the exactness risk.

## 9. What blocks the port and must be prepared beforehand

- **The monolith.** `mock_backend.py` is 4092 lines with more than two hundred definitions in one
  file.
- **Global state:** the OCCT lock, the numpy flag, cache and environment constants — all at module
  level.
- **Tests depend on private names:** `test_tangent_policy.py` and `bench_viewer.py` import
  `_tangent_families`, `_pack_edge_segments`, `_prepare_ordered` and others. In a port those names
  become a public contract: either stabilise them, or move the tests together with the oracle.
- **Tuples and dicts instead of structures:** path points, stages, cylinder and cone descriptors,
  tool profiles. **Resolved (0.13.13):** module-boundary types live in `gv_types.py`; a dict remains only
  where QML reads it.
- **Magic numbers without a name or unit** — the port must name them, or exactness drifts silently.
- **`print` instead of logging** — in C++ these become `qCInfo`/`qCWarning` categories.
- **No type annotations** — for C++ that means "guess the type"; stage 0 is mandatory.

## 10. Summary

1. Start with **stage 0** — without the oracle and types, bit-for-bit is unreachable.
2. Then stages 2–3: the pure core and the Qt geometries — cheap, safe, and good practice.
3. **The main gain is stage 4** (STEP, STL, normals, silhouette directly through the OCCT SDK).
4. The controller (stage 5) is needed to let go of Python entirely; freeze the QML contract and the
   buffer formats and move them 1:1.
5. **What will not pay off:** `GridGeometry` and `AxisArrowGeometry`, the Python fallbacks, the
   render acceptance tools, the camera JS; the disk cache is a separate decision.
6. **The main risk is exactness, not size:** without control over summation order, FMA and rounding,
   the golden checks will fail.

## 11. Optimisation and hardware adaptivity (after the port)

Found by reviewing the C++ after stages 1-5; ordered by payoff.

**Structure and duplication.**
- Weld-by-grid is implemented three times (`core/normals.cpp`, `core/outline.cpp`, `core/silhouette.cpp`),
  with `roundToInt`, `norm3` and `dot3` duplicated alongside — they belong in one internal header.
- STEP reading is duplicated in the plugin (`stepledgesgeometry.cpp`) and in `occt/step_import.cpp`; the
  plugin also re-reads the file from disk on a `deflection`/`hideFormEdges` change — the shape is not cached
  (about 0.6-0.9 s per property change; the `step.shape_read` baseline is 858 ms).
- Tessellation runs twice over the same shape: `occt/step_surfaces.cpp` calls
  `BRepMesh_IncrementalMesh` after it has already run inside `loadStepBundleAll`.
- The edge coalescing (`occt/step_edges.cpp`) is not used by the bridge: `gv_load_step_model` returns the
  edges before coalescing, and Python does the coalescing.
- `gvqtgeom` builds but is wired only to a probe; `emptyCylinderFamilies`, `packedPositions`, `vertsBounds`,
  `loadStepModel(string)` and `parseUi*` are outside the ABI — they live only in probes.

**Data copies.** The STEP soup (136194 vertices, about 3.3 MB) is materialised 6-7 times on the way to the
buffers (about 20-25 MB of transient copies per load), the normals (about 1.6 MB) up to five times. The
reasons: the soup returned by value in `orientShellOutward`, explicit copies in `step_bundle.cpp`,
`malloc`+`memcpy` in the bridge, `_take` in `gv_cpp.py`, then copies inside the geometries. The NC points
are copied 3-4 times.

**Per-frame hot path.** `gv_mesh_silhouette_segments` and `gv_tangent_segments` rebuild all arrays on every
call: about 2.6 MB of copies per frame at 45398 faces and 57842 edges. The cure is an opaque handle on the
topology and the families (built once per model) — an ABI change, `GV_ABI_VERSION` 1 -> 2.

**OCCT dependencies (investigated by measurement).** What is really used: `STEPControl_Reader`,
`BRepMesh_IncrementalMesh`, `Poly_Triangulation`, `TopExp_Explorer`, `TopoDS`, `BRep_Tool`, `TopLoc`,
`gp_Trsf`, `BRepAdaptor_Curve/Surface`, `GCPnts_UniformDeflection`, `gp_Cylinder`/`gp_Cone`. Measurements
with `dumpbin /dependents` on the built artifacts and `Get-Process -Module` on the running viewer (OCCT is
the conda-forge `occt 8.0.1` package, `all_h6eddace_201`, `D:\cnc-toolchain\occt-env\Library`) give the
real runtime closure: 23 of the 53 `TK*.dll` (38 780 416 B = 37.0 MiB) plus 22 non-`TK` DLLs from the same
install (9 669 120 B = 9.2 MiB) - 45 DLLs, 46.2 MiB in total, which is exactly what the process maps. They
are `TKDESTEP`, `TKernel`, `TKMath`, `TKG2d`, `TKG3d`, `TKGeomBase`, `TKGeomAlgo`, `TKTopAlgo`, `TKBRep`,
`TKShHealing`, `TKMesh`, `TKXSBase`, `TKPrim`, `TKBO`, `TKHLR`, `TKDE`, `TKCAF`, `TKLCAF`, `TKCDF`,
`TKVCAF`, `TKV3d`, `TKService`, `TKXCAF`; the 22 non-`TK` ones come through `TKService` -> FreeImage/freetype
and their image chain (`zlib`, `jpeg8`, `tiff`, `openjp2`, `libwebp*`, `libsharpyuv`, `raw`, `OpenEXR*`,
`Iex`, `Imath`, `IlmThread`, `deflate`, `liblzma`, `Lerc`, `zstd`, `lcms2`, `openjph`, `libpng16`).

- **Correction 1: the set is 23 DLLs, not 13-14.** The earlier assumption that visualisation and OCAF can
  be switched off did not hold. `dumpbin /dependents TKDESTEP.dll` shows it pulls `TKDE` and `TKXCAF` (and
  through them `TKLCAF`/`TKCDF`/`TKCAF`/`TKVCAF`/`TKV3d`/`TKService`/`TKBO`/`TKPrim`/`TKHLR`), so a build
  without OCAF/XCAF while keeping STEP is impossible: `TKDESTEP` would not link. The conda recipe has no
  OCAF/visualisation switch at all - they are always built.
- **Correction 2: `novtk` shrinks the disk, not the runtime.** The conda `novtk` build differs from the
  `all` build by exactly one file, `TKIVtk.dll` (201 728 B): 51 files / 56 926 208 B against 52 files /
  57 127 936 B, and all 23 needed `TK` DLLs are in both. So `novtk` does not shrink the runtime (the same
  45 DLLs are mapped), but `all_` depends on the `vtk` package while `novtk` does not, and dropping `vtk`
  removes from the environment 177 `vtk*.dll` (207 799 808 B = 198.2 MiB) and 24 `viskores*.dll`
  (51 138 560 B = 48.8 MiB) - about 250 MiB of DLLs on disk.

**Size.** The whole `Library\bin` is 552 DLLs / 1 091 342 102 B (1040.8 MiB); with the Qt DLLs that were
moved aside, 637 DLLs / about 1147 MiB. Only 45 of them (46.2 MiB) are needed: about 99 % of the install is
dead weight for us.

**Speed.** Nothing is left that speeds OCCT up without changing the produced triangles. The mesh deflection
and the angular deflection (`BRepMesh_IncrementalMesh(shape, deflection, false, deflection*2.0, true)`) both
change the triangulation, that is, they break the golden sha256, and they are already exposed through the
`CNC_QUALITY` presets (`med` 45398 triangles / 129 ms, `low` 7856 / 63 ms, `high` 175808 / 422 ms, `safe`
4132 / 56 ms). No `TK*.dll` imports `tbb12.dll`, the conda recipe builds with `USE_TBB=OFF`, and
`isInParallel=true` already uses OCCT's internal pool, so a TBB build would gain nothing. The conda build is
already Release with LTO; `/fp:fast` would break the golden. `STEPControl_Reader` already reads geometry
only (colours and names come from `STEPCAFControl_Reader`). The remaining lever is the STEP shape cache
(item 2).

**Startup.** The process maps 120 modules, 45 of them from the OCCT install. Measured `gcodeviewer.exe`
offscreen (median of 5-6 runs): Qt only 24-28 ms; QML and plugin without OCCT 42-43 ms; plugin with OCCT
loaded but no program parsed 61-63 ms - so mapping the whole OCCT closure costs about 19 ms; a full
offscreen run of `Test_model.ngc` is 748-751 ms. Trimming the install on disk does NOT speed up the start
(the loader resolves DLLs by name, not by scanning directories); only mapping fewer DLLs would, and a
visualisation/OCAF-free build would save about 35 modules and about 19 ms - which is unreachable without
patching `TKDESTEP`.

**Conclusion.** The remaining practical gain is the distribution size: ship 45 DLLs / 46.2 MiB instead of
about 1147 MiB and drop the `vtk` package; the speed gain is only about 19 ms of loader time. Linking stays
dynamic: LGPL with the OCCT exception, and we have two OCCT consumers (the bridge and the plugin).

**Version-dependent places.** Tessellation is tied to OCCT's deflection behaviour (changed in 7.4), so an
SDK version bump re-approves the golden. In the code: `OCCT_HANDLE_NOCAST`, the 5-argument
`BRepMesh_IncrementalMesh` (with `isRelative`, since 7.5), the 2-argument `GCPnts_UniformDeflection` (up to
7.6), and the 1-based `Poly_Triangulation::Node/Triangle` (deprecated in newer branches).

**Eigen is not used and is not needed.** There is not a single Eigen include under `cpp/**`: bit-for-bit is
already secured by a hand-written summation order (`pairwise` and buffered sums), while `Eigen::cross()` and
`.norm()` produce a different order and FMA. The Eigen mentions earlier in this document are historical.

**Parallelism.** There is no `std::thread`, no OpenMP and no `QtConcurrent` in the C++; the only parallel
call is `BRepMesh(isInParallel=true)`. Worth parallelising: the per-face soup assembly with concatenation in
the original order, the cone policy over the cones (hoisting the temporary vectors out of the loop), and the
outline `p0/p1` assembly. Sorts and sums must not be parallelised: their order is part of the golden.

**Hardware variability.** Presets `low`/`med`/`high` plus the fallback `safe`; the knobs and the
auto-detection are in `DESIGN.md` §13 ("Performance and hardware adaptivity"). No hard-coded thresholds are left:
the outline simplification thresholds, the silhouette angle, the cone policy and DROP have moved into the preset
(`cpp/core/quality.h`, 0.32.0), together with the former arguments (`deflection`, edge and coalescing tolerances,
`weldMm`, `creaseDeg`). The NC parse and the collision scan are not changed by the level: their numbers are still
read from the environment (`CNC_DECIMATE_TOL`, `CNC_ARC_DECIMATE_TOL`, `CNC_COLLISION_STEP`).

**Qt runtime constraints (established by experiment).**
- the PySide6 process carries its own Qt 6.12.0; the loader resolves `Qt6Core.dll` by name to the already
  loaded PySide6 Qt, so `C:\Qt` is not needed in `PATH` and must not get there;
- ordinary SHARED libraries with Qt objects (the `gvqtshim` shim, the bridge) load into the PySide6 process
  unconditionally: `shiboken6.wrapInstance` yields a live object and the ownership stays with the C++;
- the QML plugin (`CNC.Edges`) loads only if the module directory and `OCCT/bin` are in `PATH` BEFORE the
  engine is created (or the companion library is preloaded via ctypes); `os.add_dll_directory` does not help
  the plugin entry point, and a load failure is cached in the process, so a retry there does not help;
- a C++ build against a different minor Qt than PySide6's still links by name to the PySide6 Qt: the ABI
  mismatch is silent, so the versions must match (6.12.0 on both sides today).

**Priority list.**
1. An opaque handle on the silhouette topology — remove about 2.6 MB of copies per call of the bridge.
   **Refined by measurement (0.31.0):** the shipped viewer makes no copies at all — the QML controller
   (`gv_toolpath.cpp`) keeps the topology and the families between frames and calls the core by `const&`, while
   the 2.6 MB (2 577 312 B: `ea`/`eb` 694 104 each, `f0`/`f1` 231 368 each, `n` 544 776, `d` 181 592) is
   assembled only in the C ABI bridge (`gv_mesh_silhouette_segments`, `gv_bridge.cpp`), that is, on the
   comparison path (`gv_cpp.py`, `test_cpp_bridge.py`, `bench_viewer.py`). The gain for the hardware is zero;
   the priority is dropped.
2. A cached STEP shape in `StepEdgesGeometry` — minus 0.6-0.9 s per property change (low risk).
3. Drop the second `BRepMesh` — one tessellation pass less on a cold load (low risk).
4. Remove the copies on the STEP -> soup path (`move` instead of a copy, in-place flip) — 10-20 MB of peak
   memory and tens of ms (low risk).
5. Presets `low`/`med`/`high`/`safe` and `CNC_QUALITY` — **done (0.32.0):** the tolerances of the model read
   and of its processing are gathered in `cpp/core/quality.h`, and the level is read from the environment once
   per model load. Measured (`bench_probe`, i5-13420H, median of three): tessellation 129.8 -> 61.3 / 416.4 /
   56.0 ms and 45398 -> 7856 / 175808 / 4132 triangles, silhouette topology 20.4 -> 3.8 / 83.3 / 2.0 ms,
   tangent families 17.1 -> 3.1 / 113.2 / 1.9 ms (the level order is `low` / `high` / `safe`); at `med` the
   numbers are unchanged. The `low`/`high`/`safe` values are proposals, only the deflection is measured; what
   remains is reconciling the non-`med` levels through the Python mirrors and the bridge.
6. Auto-detect the level from the renderer/GL version and a reference frame run (medium risk: there is no
   VRAM-size API, and the RHI behaviour on ION can only be checked on hardware).
7. Buffer reuse in `meshSilhouetteSegments`, `tangentFromFamilies` and `HLRLineGeometry` —
   **done (0.34.0):** the claimed 0.1-0.5 ms per frame is not there: the whole per-frame path is about
   0.09 ms, and the allocations are about 5 us of the ~95 us frame path (about 6 %). The real per-frame cost
   is arithmetic: the sign loop over 45398 faces and the scan of 57842 edges. Reusable scratch buffers were
   added (a `MeshSilhouetteScratch` and a `TangentScratch` overload in `cpp/core/silhouette.{h,cpp}` and
   `cpp/core/tangents.{h,cpp}`, a `vert_` member in `cpp/qt/gv_geometry.{h,cpp}`, and the scratch members in
   `cpp/qmlbackend/gv_toolpath.{h,cpp}`); the old function signatures stayed as overloads that move the
   scratch result out, so the bridge and the probes are unaffected. Allocations per frame went from 21 to 0
   (mesh silhouette) and from 31 to 0 (tangent families); the ribbon buffer (64.5 KB for the silhouette,
   683 KB for the outline) is no longer allocated and freed every frame, which also removed a dip of up to
   0.25 ms in `frame setSegments_edges` where the freed memory was returned to the OS and had to be committed
   again. A/B inside one binary (frame_reps 300-500): mesh silhouette 0.0873-0.0934 ms -> 0.0791-0.0868
   (3-7 us, 4-8 %), analytic tangents 0.0090-0.0094 -> 0.0071-0.0082 (about 18 %). The time gain is small
   (about 5-6 us per frame, about 6 % of the frame path), and the run-to-run spread of a single build is of
   the same order, so the allocation count and the removed dip are the solid evidence, not the microseconds.
   `setSegments_edges` and `packedPositions` are NOT per-frame phases - they run when a model is published -
   and the plan's list should not imply otherwise.
8. Parallelising the cold stages — **checked by measurement (0.33.0), no change needed:** the three targets
   together are about 0.5 % of the cold load (about 690 ms; the STEP read 376 ms and `BRepMesh` 124 ms are
   OCCT itself). The `step_tessellate` split (129.8 ms): `step_mesh` 123.8-125.5 ms (96-97 %, OCCT's mesher,
   already `isInParallel=true`) plus `step_soup` 2.4-3.0 ms (about 2 %); the cone policy costs <= 2 ms inside
   `step_families` (16.1-17.2 ms), and the `p0/p1` outline 0.050-0.053 ms for 2426 segments. A parallel soup
   assembly (per-face blocks concatenated in the original face order) kept the byte order but measured
   2.79-2.97 ms against 2.43-3.02 ms sequential — neutral, so it was reverted. The loops where the time
   really sits (`step_topo` 20 ms, `step_families` 17 ms, `ngc_parse` 70 ms) cannot be parallelised: their
   order is part of the golden. The probe gained the phases `step_mesh` and `step_soup` for this.
9. Drop the duplicated STEP reads and the dead code, and hoist the shared helpers — **done (0.34.0):** the
   duplicate STEP read is gone: `STEPControl_Reader` now exists in exactly one place, `gv::readStepShape`
   (`cpp/occt/step_import.cpp`); the edge plugin, the probes, `step_bundle`, `step_surfaces` and
   `bench_probe` all call it. The shared numeric helpers are in `cpp/core/np_helpers.h` (`dot3`, `cross3`,
   `npMod`, `norm3Sqrt`, `norm3Pow`, `roundToInt`). The remaining item of the plan's "Structure and
   duplication" block - the grid weld written three times (`cpp/core/normals.cpp`, `cpp/core/outline.cpp`,
   and the silhouette's own grid) - is deliberately NOT merged: the three have different tolerances
   (1/1000 mm against the silhouette's 1e-4 mm) and different semantics (a hash map against a sorted
   `np.unique`), and `normals.cpp` iterates an `unordered_map` whose order affects the accumulation order of
   the normals, so merging would touch the golden contract for no gain.
10. The outline numbers are reconciled (0.31.0): 18986 is the STL outline before simplification
    (`test_outline_segments.py`; 1435 after simplification), while 4884 and 2426 are the STEP edges before and
    after coalescing (`test_stepledges_plugin.py`). These are different quantities, not a discrepancy; the
    confusion in the `refreshTangents` docstring (`controller.py`) is fixed.

**Status and measurements (part 1 done, 0.28.0).** A `bench_probe` was added; the measurements on an
i5-10400F (this is NOT an Atom/Ion) show where the time actually goes: STEP read 610 ms and tessellation
185 ms (OCCT itself), NC parse 174 ms, surfaces 70 ms, silhouette topology 35 ms, normals 28.5 ms, tangent
families 25 ms; per frame: mesh silhouette 0.14 ms, analytic 0.019 ms. Done: the copies on the STEP -> soup
path are gone (six soup materialisations became three, four normal ones became two; ~1.6 ms and 13 MB of
peak), the second `BRepMesh` on the `loadStepBundleAll` path is gone (measured: a repeated `BRepMesh` on an
already-meshed shape costs 46.9-50.0 ms, not an empty pass; the runtime does not pay it — it passes the
already-meshed flag — kept as hygiene), dead code removed, the shared numeric
helpers moved into `core/np_helpers.h`. Corrections to the list above: `packedPositions` is NOT dead (called
at runtime from `gv_toolpath.cpp`), and the "one tessellation pass less" claim did not hold. The main
remaining targets by payoff are the NC parse (176 ms) and the STEP shape cache in the plugin (0.6-0.9 s per
deflection change), not the items 1-4.

**Status and measurements (parts 2-4 done, 0.30.0 and 0.31.0).** The STEP shape cache in `StepEdgesGeometry`: a
`deflection`/`hideFormEdges` change no longer re-reads the file — 550.1 -> 6.9 ms on an i5-13420H, and of the
former 550.1 ms the STEP read took 466 ms; the edge counts are the same (2426 and 4884). NC parse: the line was
prepared twice over 110585 lines, `findWords` allocated a `std::string` for each of the 342038 tokens, and
`strtod` was called even for letters whose values are never read — `ngc_parse` 108.4 -> 69.7 ms (`bench_probe`,
i5-13420H). The comparison oracle no longer depends on the numpy version (scalar sums instead of a batch
`matmul`, stable segment sorting, an explicit face index width), and `oracle_manifest.json` was re-based to a
single record (`step.segments`, order). Items 1 and 10 of the priority list are closed by the measurement
refinement.
