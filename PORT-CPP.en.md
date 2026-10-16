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
| numpy for `cross`, `norm`, `einsum` | Eigen (`Vector3d`, `.cross()`, `.norm()`) | No intermediate ndarray objects; the 36 optional-numpy branches go away |
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
  at all until they are installed. Eigen is header-only, licence MPL2.
- **Cross-platform:** Qt, OCCT and Eigen build on Windows and Linux alike; but the target weak
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
| **1** | Build skeleton: CMake, Qt6 Quick3D, OCCT, Eigen; make `cpp/stepledgesgeometry` buildable; wire the build into the guard | The plugin builds, its segments match `load_step_model` | The plugin is separate and breaks nothing |
| **2** | Pure core: G-code parser, cycles, arcs, `UI_*` tags, `_prepare_ordered`, stage packing | C++ gives identical buffers and stages against the oracle | The Python core stays |
| **3** | Qt geometries: `ToolpathGeometry`, `MeshGeometry`, `ToolGeometry`, `HLRLineGeometry` with the buffer formats | `smoke_test_qml`, `test_ribbon_width` green, buffers match | The Python geometries stay |
| **4** | **STEP, STL, normals, topology, silhouette** on the OCCT SDK directly + Eigen | `test_normals_golden` bit-for-bit, `test_normals_orientation`, `test_tangent_policy` green, cold loading down several times over | The Python STEP branch stays as the diff reference |
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
the Ninja generator (MSVC, Qt 6.12 Quick3D, OCCT 8.0.1, Eigen) into `build/edges`. The STEP edge segments
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

**Stage 4 gives the bulk of the gain.** Stages 2 and 3 are cheap and safe; stage 5 is needed to let
go of Python altogether but barely speeds anything up on its own.

## 8. Estimates

Assumptions: a direct translation of the algorithms, Eigen instead of numpy, OCCT linked
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
