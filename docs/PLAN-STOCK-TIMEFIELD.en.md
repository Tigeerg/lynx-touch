# Material removal simulation, third variant: the cut as a space-time field

A working document next to `PLAN-STOCK.md` and `PLAN-STOCK-IMPLICIT.md`. The third variant
changes not the part but the **class of the problem**:

- plan 1 — a height map, counted from the tool (path-centric), the error is removed by a closed
  envelope of the move;
- plan 2 — a volumetric implicit field, counted from the tool in 3D, the surface is extracted;
- plan 3 — **counted from the field (field-centric)**: the cut is not "N tool passes
  stamped out" but the **lower envelope of the tool bodies = a generalized weighted
  distance transform**, and the cut itself is stored as a **field in time**.

The source of truth for the current code is `cpp/core/stock_sim.{h,cpp}`, `stocksurface.*`,
`qmlbackend/gv_*`, `DESIGN.md` (the "Material removal simulation" section). Numbers are a measurement or
the word "proposal". The rules are `CLAUDE.md` and `VERSIONING.md`.

## 1. Three pillars

### Pillar 1. The envelope as a distance transform (not a pass over the moves)

A height map is a minimum along the path:
`H(X, Y) = min over p from the path [ z_острия(p) + h( |(X, Y) - p| ) ]`,
where `h(r)` is the profile of the cutting part (the same table as today). This is the **lower
envelope of the tool bodies** placed along the path.

- For a **ball** cutter this is literally the **signed distance to a set of balls**:
  the centres `C(p) = (p.x, p.y, z_острия(p) + R)`, and `H(X, Y)` is the zero level of
  `min over p [ |(X, Y, z) - C(p)| - R ]`. Not "12 chords" but a true sphere, and not a
  sampling of positions but an exact function.
- For a **flat** cutter `h(r) = 0`: `H = min z_острия(p)` over `p` no farther than the radius —
  this is a **weighted Voronoi diagram** (additively weighted: each "seeder" has its own
  weight `z_острия`).
- For a cone/chamfer it is the envelope of cones, also a generalized distance.

Hence: the cut is a **generalized (additively weighted) distance transform**
(Generalized Voronoi / power diagram), and it does not have to be counted by a pass over the moves.
It is counted by "first arrival" solvers:

- **Fast sweeping method** (fast sweeping, Zhao 2005) — solving the eikonal equation
  `|grad T| = 1` in a few passes over the grid, O(N) in the resolution;
- **Fast marching method** (fast marching, Sethian) — the same as a growing front;
- **Jump flooding on the GPU** (Rong and Tan, 2006; for volume rendering — Schneider, Kraus,
  Westermann, 2010) — parallel, with a tiny constant, O(N^2 log N); the power diagram variant
  covers the weights.

Metaphor: **the cut goes as a front**. The tool does not "stamp" points but sends a wave from
the trajectory; where the wave passed first, there is the cut surface. A front, not a loop.

Honest about accuracy: jump flooding is **approximate** (isolated errors per pixel),
so it has variants with a cleanup pass (JFA+2, 1+JFA). For an NC verification tool
this means: **the authority is the fast sweeping method on the CPU (exact), the GPU is an
accelerator with a control cleanup pass**. The order of sweeps is fixed, so determinism is
achievable; a bitwise digest additionally requires a **fixed tie-breaking rule** (equal
distances) — this is named, not hidden.

### Pillar 2. The chronicle of the cut: time as a dimension

The cut is monotone: what is removed does not come back. So for each node one can store not
only the height but also the **path length at which the node was cut** — `t_рез`. Then the
field at "moment" `t` is the nodes with `t_рез <= t`.

- **Rewind becomes free**: today going back along the path is a full recompute of the
  prefix, 200-232 ms, and at a 0.2 node up to 4 s (`DESIGN.md`). With the chronicle it is a change of
  one number, O(1), with no recompute.
- Playing back the cut is not a recompute every 100 ms but a **shift of the threshold**; on
  the GPU the threshold is one uniform, a frame of any speed.
- Memory: plus one float32 per node (time). This is like the "journal of contributions" from
  plan 1, but not an optimisation of the rewind — rather the **main representation**: the machine
  state at any moment in time is recovered from one function.

Metaphor: the stock is a **4D object** (space plus time), not a snapshot of "now".
The simulation is a chronicle from which the viewer takes a frame.

### Pillar 3. Progressive delivery: the part on demand, not the whole grid

A frame must not depend on the size of the stock. Hence the techniques proven in terrain and
medical visualisation:

- **Geometry clipmaps** (Losasso and Hoppe, 2004; going back to Tanner, Migdal,
  Jones, 1998): a windowed pyramid of levels around the region of interest — detailed up close,
  coarse far away. The frame ceiling is set by the window, not by the stock.
- **Layered depth images** (layered depth images, Shade et al., 1998) as an
  emergency path: several depth layers per screen pixel, a view-dependent
  representation — cheap, expresses undercuts, familiar to medical visualisation.
  The extreme form: "the simulation is the history of the frame buffer".
- **Out-of-core bricks** (out-of-core bricks): a large stock streams from disk
  in blocks, only the visible and active parts are in memory.

### Addendum: an exact oracle on a CAD kernel and a deviation map

Industrial NC verification ends not with a picture but with a **number**: how far the
remainder differs from the target part. The swept volume can be built once as an honest
B-rep body and subtracted with a boolean operation — **OCCT is already among the project's
dependencies**. For thousands of moves this is expensive and fragile, so **not interactive but a
reference**: on small programs a probe checks the fast field against the exact boolean
difference — this way the approximation gets an oracle, not "by eye". From there a
**deviation map** (remainder against the target part) naturally grows — that for which
verification exists, and the same as the medical task of "matching the plan and the fact".

## 2. Where the techniques come from

- Distance transform, Voronoi diagram, power diagram, jump flooding — Rong and
  Tan (2006); a GPU distance transform for volume rendering — Schneider, Kraus,
  Westermann (2010).
- The eikonal equation and first arrival — Sethian; fast sweeping — Zhao (2005).
- Clipmaps — Losasso and Hoppe (2004); Tanner, Migdal, Jones (1998).
- Layered depth images — Shade et al. (1998).
- Exact booleans over B-rep — standard practice of CAD kernels; here the kernel is already in the tree
  (OCCT).
- NC verification as a deviation map "remainder against the part" — standard practice of
  industrial program checking; medicine gives the same scheme as matching the plan and
  the fact.

None of this requires a new dependency: the solvers and the structures are our own code.

## 3. How it looks as a whole

1. **Seeders.** The path is split into seeder points (no denser than the node) with the weight
   `z_острия` (for a ball — the centres of the balls).
2. **Solver.** The lower envelope is counted by first arrival: fast sweeping on the CPU
   (authority), jump flooding on the GPU (accelerator, with a control cleanup pass). The output is
   the height and `t_рез` per node.
3. **Chronicle.** The field and the time lie side by side; a frame at moment `t` is a threshold
   by time.
4. **Delivery.** The clipmap cuts the grid by window; on a weak machine — layered depth
   images; on a large stock — out-of-core bricks.
5. **Oracle.** On small programs the exact B-rep difference confirms the field; the deviation
   map shows the remainder against the part.

## 4. Work plan

### Stage 0. Reconnaissance by measurement (a prototype, not entering the tree)

- 0.1 Fast sweeping against the current pass over the moves on `Test_model`: whether the field
  matched (outside the old step band) and by what factor it is cheaper.
- 0.2 Jump flooding on the GPU: accuracy against the CPU solver, whether the control
  cleanup pass brings the errors to zero; a frame.
- 0.3 Clipmap and layered depth images: the cost of a frame against the size of the stock.
- Exit criterion: there is a solution and numbers; the prototype does not enter the tree (a
  `DESIGN.md` requirement).

### Stage 1. Core: the distance transform and the chronicle (pure, without Qt and OCCT)

- 1.1 Seeders from the path: the weight `z_острия`, a ball as centres; the seeder step from the
  node.
- 1.2 A first-arrival solver (fast sweeping) on the CPU: the height and `t_рез`; for a
  flat cutter — a weighted Voronoi diagram, for a ball — a weighted Euclidean distance.
- 1.3 Fixed tie-breaking and order of sweeps — for determinism and bitwise
  digests.
- 1.4 Chronicle: `t_рез` per node, the field at moment `t`, a rewind as a threshold.
- Checks: a new probe `front_stock_probe` — flat (the match with the analysis
  is exact, there is no old `cell/4` band), ball (a true sphere), cone; **a match with
  the field of the current `simulateStock`** where both are defined; a rewind as a change of
  the threshold equals a pass from the start (a digest); a rewind O(1) by measurement;
  determinism when the order of sweeps changes.
- Criterion: the deviation from the analysis is no more than 1e-6 mm; the step band is absent.

### Stage 2. Drawing and delivery

- 2.1 The chronicle in the shader: a frame at moment `t` is a threshold by time; playback and
  rewind without recompute and without re-uploading the texture.
- 2.2 A grid clipmap: a window of levels around the region of interest; the frame ceiling does not
  come from the stock.
- 2.3 Layered depth images as an emergency path on weak hardware (chosen by
  hardware and level, like the `safe` choice).
- Checks: a `stock_bind_probe` double — the integrity of the publication, a frame rewind without
  recompute; a comparison of what is drawn with the field; field frames on `bench_probe` (stage 0.1
  `PLAN-STOCK.md`).

### Stage 3. Oracle and deviation map

- 3.1 The probe `brep_oracle_probe`: on a small program it builds the swept volume and
  subtracts it from the stock with an OCCT boolean operation (`step_bundle`/OCCT already in the tree),
  and checks the fast field against the exact difference.
- 3.2 A deviation map: the remainder against the target part (STEP/STL from
  `cad-testdata`), numbers in mm, output as a line; to the screen — later, if needed.
- Checks: the field against the B-rep difference within a tolerance derived from the node; the
  deviation is deterministic.

### Stage 4. Acceptance, documents, versions

- 4.1 The six `--manifest` probes and `oracle_manifest.json` are not touched.
- 4.2 `DESIGN.md` and `DESIGN.en.md`: extend the "Material removal simulation" section with the
  third representation (distance transform, chronicle, delivery, oracle), in one pass in both languages.
- 4.3 `modules/gcode-viewer/CHANGELOG.md`, `VERSION`, `projects.json`; MINOR per stage.
- 4.4 Commits `feat(gcode-viewer): ...` per stage; checks per `VERSIONING.md` section 5.

## 5. Risks and what we reject

- **A generalized distance for an arbitrary profile.** The exact link "profile -> metric"
  is proven for a ball and a plane; for a chamfer and a cone the metric is anisotropic, and it must
  either be derived or the per-site stamping of plan 1 must be kept. Stage 0.1 decides.
- **Jump flooding is approximate.** Either a cleanup pass to zero errors (JFA+2, 1+JFA), or the
  GPU for preview only and the CPU as the authority. For NC verification "almost exact" is not
  good enough.
- **The OpenGL 3.3 branch and weak hardware.** The clipmap and the layered images are
  fallback paths; a rollback to plan 1 or 2 is always possible.
- **The cost of `t_рез`.** Plus one float32 per node and a monotone write; in the Test_model
  stock height map this is another 236 KiB at a 0.5 mm node — a named price, not a hidden one.
- **We reject:** neural surrogates of the field (unverifiable and impossible to justify on
  weak hardware), full FEM and haptics (not about drawing and NC verification).

## 6. Comparison of the three plans

| | Plan 1 (height map) | Plan 2 (volumetric field) | Plan 3 (field and time) |
|---|---|---|---|
| Direction of the count | from the tool | from the tool, 3D | from the field (first arrival) |
| Core of the math | a closed envelope of the move | a 3D body subtraction | a distance transform |
| Undercut | refusal | expressed | expressed (layers/ray) |
| Chronicle of time | a journal of contributions (option) | a journal of blocks | the main representation |
| Rewind | a prefix recompute | a journal | O(1), a threshold |
| Frame from size | grows with the stock | grows with the band | limited by the window (clipmap) |
| Oracle of accuracy | analysis | analysis + marching cubes | OCCT B-rep |
| Belief in the check | analysis | analysis | + a deviation map |
| Complexity | medium | high | high |
| Risk on weak hardware | low | medium | medium (there are rollbacks) |

Recommendation: the three plans do not compete but divide the tasks.

- **The near step** — plan 1: accuracy and speed in the current architecture, guaranteed
  to fit into weak hardware.
- **Undercuts and multi-axis** — plan 2: a volumetric field, enabled where the map
  does not express the shape.
- **Scale and time** — plan 3: the distance transform removes the dependence on the number
  of moves, the chronicle gives an instant rewind and playback, the clipmap holds the frame, and
  the B-rep oracle and the deviation map turn the picture into a program check.

## 7. Open questions

- The metric for a chamfer and a cone: an exact anisotropic one or keep the stamping of plan 1.
- The accuracy of jump flooding: whether a cleanup pass to zero errors is needed on the live branch
  or the GPU stays a preview.
- The capacity of `t_рез`: a plain float32 or packing the time (there are many nodes, and the path is monotone).
- Whether an English mirror file (`PLAN-STOCK-TIMEFIELD.en.md`) is needed before the transfer into
  `DESIGN.md`, or a mirror at stage 4.2 is enough.
