# Material removal simulation: methods, evaluation and work plans

A working document, not a description of what has been done. It merges the former plans
(`PLAN-INDEX.md`, `PLAN-5-METHODS.md`, `PLAN-STOCK*.md`): an overview of the approaches, an evaluation by
the numbers and four detailed plans. The source of truth for the current code is
`cpp/core/stock_sim.{h,cpp}`, `stocksurface.vert/frag`, `stockshell.*`, `stockcutcontour.*`,
`stockcutwall.*`, `qmlbackend/gv_*` and the "Material removal simulation" section in `DESIGN.md`. The
numbers are either taken from measurements (with the source named) or labelled as an estimate. The rules
are `CLAUDE.md` (comment language, `->` instead of an arrow glyph, units in names) and `VERSIONING.md`
(the order of editing, the checks, the versions; an experimental branch is `<base>+<tag>.<n>`).

## 1. Overview: five approaches

Each approach balances speed, accuracy and support for complex shapes (undercuts, multi-axis machining).

1. **Height map.** The space is a 2D grid, a node (X, Y) holds one depth Z. Removal is
   `Z_new = min(Z_old, Z_tool)`. Pros: lightning speed, little memory, simplicity. Cons: does not express
   undercuts, hard for multi-axis. The workhorse of 2.5D.
2. **Volume field (SDF).** Every point in 3D holds the distance to the nearest surface; the surface is the
   zero level. Pros: mathematical accuracy, boolean operations. Cons: CPU load. The oracle of accuracy.
3. **Chronicle of the cut (Time-field).** A node holds not only the depth but also the time (or `pathDist`)
   at which it was cut. Pros: instant rewind (O(1)). Cons: extra memory. Interaction and NC debugging.
4. **SDF voxels (VDB / Octree).** Sparse blocks with SDF values: emptiness and solid metal are not stored.
   Pros: smoothness, any voids. Cons: complexity of the data structures. Complex relief and threading.
5. **Ray marching.** No geometry is built in advance; the surface is found by a ray in the shader over the
   SDF. Pros: an ideal picture with no grids. Cons: fully on the GPU, complex shaders. High-quality preview.

### Summary table of the choice

| Method | Speed | Accuracy | Complexity | Main task |
|---|---|---|---|---|
| **1. Height map** | High | Medium | Low | Ordinary 2.5D milling |
| **2. Volume field** | Low | High | High | Verification (oracle) |
| **3. Chronicle of the cut** | High | High | Medium | Operator convenience (rewind) |
| **4. SDF voxels** | Medium | Very high | Very high | Threading, labyrinths, voids |
| **5. Ray marching** | Very high (GPU) | Ideal | High | An ideal picture with no grids |

Four detailed plans then turn these approaches into work: plan 1 - a height map (method 1), plan 2 - an
implicit volume model (methods 2 and 4), plan 3 - the field and time (method 3), plan 4 - screen-space CSG
(method 5 in the form of swept bodies).

## 2. What exists today

**In `main` (viewer 0.58.3).**

- Representation: a height map `nx x ny` over machine X/Y, at a node the depth from the top of the stock
  (float32 -> R32F), index `iy * nx + ix`.
- Removal: `StockField::plunge` at one position, `StockField::sweep` walks the 3D length of the segment with
  a step of `cellMm * 0.5`, the field takes the `max` of the depth (that is, the `min` of the height). The
  lower envelope of the profile `h(r)` cuts up to the stick-out; the holder does not cut.
- Profile: `StockCutter`, a radial table `h(r)` with linear interpolation; the ball is sampled with 12
  chords, the chamfer with 8.
- Undercut (`LOLLIPOP`, `DOVETAIL`, `T_SLOT`/`TSLOT`, a profile wider at the top): the walk refuses
  entirely, there is no field.
- Live walk: `StockWalk` keeps the field and the cursor, continues from the last whole move, and on a step
  back along the path resets and walks from the start.
- Rendering: one vertex per node, two triangles per cell, the whole grid; the vertex shader subtracts the
  height with a nearest fetch; the normal is a central difference over `NORMAL_SPAN = 2`; the colour is a
  threshold `cutMm = 0.05` on `vDepth`.
- Publication: a copy of the whole field into a `QByteArray` on the worker thread, a whole upload of
  `QQuick3DTextureData` once per 100 ms (it has no partial upload).

**On the experimental branch `alt/stock-sim`** the exact sweep `sweepExact` and its own boundary are
already done; their numbers are given below by reference to the branch journal
(`modules/gcode-viewer/CHANGELOG.md`, records `0.58.1+alt.N`). Those numbers are not in `main`.

Measurements of `Test_model.ngc` (70 914 moves, 137.8 m, `UI_STOCK` extent 150x100x40 mm):

| What | node 0.5 mm | node 0.2 mm | Source |
|---|---|---|---|
| Nodes | 301x201 = 60 501 | 751x501 = 376 251 | `quality.h:70-78` |
| Field cost (sampling) | 192.4-425 ms | 2288.6-8393 ms | `quality.h` |
| Field cost (exact, `alt`) | 118.1 ms | 790.6 ms | `0.58.1+alt.1` |
| Field memory | 236 KiB | 1.44 MiB | `quality.h` |
| Ball against the analysis | 1.93e-8 mm (the 12 chords gave 8.35e-3) | - | `0.58.1+alt.1` |
| Boundary error by the threshold (wall/ball) | 0.475 / 0.223 mm | 0.190 / 0.044 mm | `0.58.1+alt.4` |

Also from `alt/stock-sim`: the exact boundary is 1.5e-8 mm against 0.475 mm by the threshold of the field;
the drawn boundary (zero level of the signed distance) is 0.011 mm at node 0.5; the tilt of the transition
face from the vertical at a step of 0.8 / 4.0 mm is 17.4 / 3.6 degrees; the rewind by threshold is 569x
cheaper than a recompute from the start, but 87.7 % of the nodes are cut again and again.

## 3. Evaluation of the volumetric methods: what we take and what we do not

### 3.1 The dense voxel - we do not take it

Stock 150x100x40 mm, one byte per voxel:

| node | voxels | memory | update cost (estimate) |
|---|---|---|---|
| 0.5 mm | 300x200x80 = 4.8 million | 4.8 MB | roughly 80 times more expensive than the map: 64 ms -> about 5 s per pass |
| 0.2 mm | 750x500x200 = 75 million | 75 MB | roughly 1000 times: 443 ms -> minutes |
| 0.1 mm | 12 billion | - | does not fit; a sparse variant is needed |

**The argument.** Our removal is 3-axis and comes from above, that is 2.5D: in each column only the top
boundary of the material matters, and a height map stores exactly that. A dense voxel pays for the whole
body of the tool down to the bottom of the stock and multiplies memory and time by the height, without
adding information. It does not save the boundary either: it is quantised by the voxel plus marching cubes
on top.

### 3.2 Other methods - in order of increasing progressiveness

- **Dexels (multi-dexel, tri-dexel).** Several heights per column: 1-2 usually, more at undercuts;
  tri-dexel - three orthogonal grids. An industry standard (Vericut and the like). The cheapest extension
  of the height map and a direct way to lift the refusal at an undercut.
- **Adaptive hierarchical maps** (a quadtree over the 2.5D field, small cells only at the boundary) -
  stage 1.3 of plan 1: a smooth boundary for almost the same memory.
- **Narrow-band level set / SDF** (OpenVDB is built this way): the signed distance is stored only in a
  narrow band at the surface, memory is proportional to the surface area, not to the volume.
- **Sparse blocks** (plan 2): the same, in blocks - convenient for the GPU and for the increment.
- **Clipmap fields** - a fine grid around the camera and the tool, coarse far away; memory is bounded.
- **A screen-space CSG tree of swept capsules with a BVH and ray marching** (plan 4): the shape is not
  discretised, the boundary is exact by construction. The cost is a BVH of tens of thousands of capsules
  and a march per frame.
- **B-rep booleans on OCCT** per move - exact, but seconds to minutes per operation: for the final check of
  one part, not for simulation.
- **Analytic volume accounting** (the intersection of the swept body with the remainder in closed form) -
  for MRR and tool life it is more precise and cheaper than any grid.

### 3.3 What we take, and in what order

1. **The boundary** - a representation of its own, exact and independent of the height map (a fine 2D field
   of the contour or an analytic contour of the union of capsules). In the limit - the screen-space CSG
   tree (given a GPU).
2. **Adaptivity and tiles** (stages 1.3 and 1.4 of plan 1): a smooth surface and less memory.
3. **Dexels** - when undercuts are needed: at a small cost, without a change of architecture.
4. **Analytic volume** - when the tool life in cm3 and a deviation map are needed.
5. **Narrow-band level set on the GPU with clipmaps** - when 4-5-axis geometry and a digital twin appear.
6. **Screen-space CSG** - as a means of evidential frame verification, if it is needed.

### 3.4 What we do not take

- **The dense voxel** - per the numbers of section 3.1.
- **B-rep booleans per move** - per the cost per operation; final verification only.
- **Depth-peeling as a data source** - it is a picture without data about the removed volume.

## 4. Plan 1 - a height map: a layered surface and an exact envelope

The order: first exact rendering (stage 1) on the current field, then a mathematically fast and exact
computation of the field (stage 2). The new method: the surface is assembled from layers with an exact cut
boundary, and the removal is computed as the exact envelope of each move over a capsule, not as a sampling
of positions; the storage is the same nodes, but sparse (tiles), with a "touched" mask next to the depth.

### 4.1 Where precision and time are lost

1. A node walked again and again: a node is covered by roughly `4r / node` positions of one move (at
   `r = 3`, node 0.2 - up to 60 times), and every position recomputes `sqrt`, `heightAt`, `max`. The main
   multiplier.
2. Discretisation of the move: the step `cell / 2` leaves an uncut strip up to `cell / 4` wide.
3. Chords instead of arcs: the sagitta of the ball's 12 chords is 0.00835 mm.
4. The whole grid: memory, triangles and the texture upload are counted over the entire stock rectangle.
5. Steps in the rendering: a piecewise-constant height, a normal over 2 texels, a binary colour.
6. `DESIGN.md` promises frame measurements of the field (the frame lines of `bench_probe`), but
   `cpp/bench_probe.cpp` has none - the frame budget of the field is not measured.

### 4.2 Rendering

- A layered surface: the top (the plane `zMax` where nothing is touched), the bottom of the cut (the height
  map) and a vertical wall along the exact contour of the cut boundary.
- The exact contour is marching squares: the depth at the boundary crosses zero linearly, the sub-cell
  position of the crossing gives a polyline, and from it the vertical quads. A pocket wall becomes a
  straight line, without steps.
- Continuous reconstruction of the height in the shader (by hand from 4-16 texels, not with a filter:
  linear filtering of R32F is not guaranteed on the OpenGL 3.3 branch). The normal in the fragment (Sobel
  or analytic), `NORMAL_SPAN` goes away.
- An adaptive mesh (restricted quadtree): a cell is subdivided only where the depth changes faster than the
  tolerance; neighbours differ by no more than one level, T-junctions are stitched. The frame cost is the
  perimeter of the cuts.
- Tiles instead of one texture: its own `QQuick3DTextureData` and its own `Model` per tile (for example,
  64x64 nodes). A tick loads only the dirty tiles; an untouched tile is not drawn and not loaded.
- Boundary AA by the signed distance to the contour, without MSAA.

### 4.3 Computation

- The exact envelope of a move. The move `A -> B`, the tool `h(r)`. For a node `P`:
  `H(P) = min over t from [0,1] of ( z(t) + h(r(t)) )`, where `z(t) = zA + (zB - zA) * t`, `r(t)` is the
  distance from `P` to a point of the segment in XY. Only nodes with `min_t r(t) <= maxRadius` are touched
  (the capsule); exactly its bbox is walked, each node once per move.
- A closed form instead of iterations. `h(r)` is non-decreasing and piecewise linear. On a piece
  `h = a + b * r` (`b >= 0`) the function `f(t) = z(t) + b * r(t)` is convex. The critical point:
  `t = t* - dz * d / ( sqrt(L2) * sqrt(b * b * L2 - dz * dz) )` when `b * b * L2 > dz * dz`, otherwise the
  minimum is at an end; here `t*` is the projection of `P` onto the line of the move, `d` is the distance
  to that line, `L2 = |B - A|^2`, `dz = zB - zA`. We clamp to the sub-interval of the piece and to
  `[0,1]`. For arcs - a safeguarded Newton with a fixed number of steps (deterministic). The
  discretisation error goes away.
- An analytic profile: primitives (line, arc, torus) next to the table; a true sphere and chamfer.
- Sparse tiles: an untouched tile is implicit (depth 0), memory and cost follow the area and the perimeter
  of the cut. Determinism is preserved: the `max` of the depth is exact and does not depend on the walk
  order.
- Live walk: the `StockWalk` cursor stays, the increment goes over the dirty tiles. The rewind is through a
  contribution log: the removal is monotone, a contribution (move index, value) is stored per tile, and the
  rewind is a binary search plus a `max` over the contributions (O(1) per node against a full recompute of
  the prefix, 200-232 ms, and on a 0.2 node up to 4 s).
- Undercut (option): instead of a refusal, a truncated dexel, up to `k` layers per column only in the
  stages with an undercut. `k = 1` is the current map, `k = infinity` is the dexel from `DESIGN.md` §13.
- GPU (option, later): the moves in an SSBO, a gather-min over the capsule in the fragment.

The estimated gain (an order, not a measurement): removing the repeated walk gives about `4r / node`
(12x at `r = 1.5`, node 0.5; up to 60x at `r = 3`, node 0.2); the sparsity adds a gain over the touched
area.

### 4.4 Work plan

**Stage 0. Baseline and budget (0.53.0, not functional).** 0.1 `cpp/bench_probe.cpp`: add the phases and
the frames of the field (`stock_frame_on`, `stock_frame_off`). 0.2 `cpp/stock_sim_probe.cpp`: take a
baseline and write the numbers into a comment. 0.3 A reference screenshot in `img/`. Checks: `pnpm guard`,
`bench_probe`, `stock_sim_probe`.

**Stage 1. Exact rendering.** 1.1 `stocksurface.vert/frag`: continuous reconstruction of the height, the
normal in the fragment, remove `NORMAL_SPAN`. 1.2 `core/stock_sim.*` and `qmlbackend/gv_stockgeom.*`: a
layered assembly (top + bottom + wall) and the contour via marching squares. 1.3 An adaptive mesh with
T-junction stitching. 1.4 Tile storage and publication. 1.5 Boundary AA by the signed distance. Checks:
the `stocksurface_exact` case, `stock_bind_probe` (the integrity of the publication), the field frames, a
before/after screenshot. Criterion: no steps on the wall, fewer triangles, a frame no more expensive.

**Stage 2. Mathematically fast and exact computation.** 2.1 `core/tooling.cpp`: an analytic profile.
2.2 `core/stock_sim.cpp`: replace `sweep` with a walk of the capsule and the exact minimum `H(P)`.
2.3 Sparse tiles. 2.4 A live walk over the dirty tiles, a rewind through the contribution log.
2.5 (option) a truncated dexel. 2.6 (option) GPU gather-min. Checks: rewrite `flat_sweep`, `ball_plunge`,
`cone_plunge`, `near_miss_step`, `stock_bind`, `stock_live`, `stock_undercut`; add an analytic case for a
slanted move with a ball. Criterion: the cost of `high` 0.2 mm falls by an order of magnitude; the
deviation from the quadrature is no more than 1e-6 mm instead of 1e-2.

### 4.5 Risks and what we reject

- OpenGL 3.3: linear filtering of R32F is not guaranteed - reconstruction by hand; the tile textures are
  verified by a prototype before the code.
- Tiles against a single texture: more scene objects; the threshold is by the frame measurement.
- Marching squares gives the boundary by linear interpolation: on a 0.5 mm node it is much more precise
  than the node, but not an absolute arc; if needed - second-order curves per cell.
- We reject for now: a full voxel/SDF-raymarch (memory and OpenGL 3.3), CSG booleans on the GPU (not
  portable to weak hardware), the dexel by default (expensive).
- Determinism: the exact method changes the bytes of the field, but the probe digests compare runs with
  each other, not with a stored reference, so `oracle_manifest.json` is not broken.

## 5. Plan 2 - an implicit volume model

A different class of representation: a volume implicit field (an implicit material function), from which
the surface is extracted separately. A height map (the current one and plan 1) by definition stores one
height per column, so it does not express an undercut and does not express an axis not along Z; the viewer
honestly refuses to compute such programs at all. Plan 2 removes both limitations: the material is a
region in 3D, the tool subtracts its swept body, and the cuts appear by themselves. The price is memory and
complexity, so this is a separate, bolder plan rather than a replacement.

### 5.1 Where the techniques come from

- Dexel, multi-dexel, three-directional dexel - Choi and Jerard (1998); Gao, Zhang, Hou (2007); Zhao and
  Qian (2009). The techniques of NC verification in applied CAM.
- Level sets and SDF - Osher and Sethian; a fast distance field - fast marching / fast sweeping, Zhao
  (2005).
- Marching cubes - Lorensen and Cline (1987); dual contouring - Ju, Losasso, Schaefer, Warren (2002),
  preserves sharp edges.
- Sparse volume blocks and the narrow band (VDB-like), TSDF (Izadi et al., 2011), ray tracing (sphere
  tracing / ray marching).

### 5.2 The method

- **Representation.** The space is divided into blocks (for example, 16^3 voxels). A block that does not
  cross the material boundary is not stored (its value is implicit). In a narrow-band voxel there is a
  signed distance `f`: `f > 0` material, `f < 0` emptiness, `f = 0` the surface. Next to it a cheap
  double - a multi-dexel.
- **Removal.** The tool is a solid of revolution; the body swept over a move is subtracted:
  `f_new = max(f_old, -g)`, where `g` is the signed distance to the swept body. For a sphere the body is a
  capsule; for a general profile - a closed-form minimum over `t` (the same convexity as in plan 1, but in
  3D). The `max` runs over all moves, the field is monotone and does not depend on the order. A cheap
  double - subtracting the body as a morphology over the intervals of a column.
- **Drawing.** Path R1 - mesh extraction: marching cubes over `f = 0`, dual contouring over the gradient
  preserves sharp edges; the mesh is adaptive and fits the `QQuick3DGeometry` contract. Path R2 - without
  a mesh: the field as a 3D texture with an indirection texture, the surface by ray tracing over the SDF.
  The choice of path is by hardware and quality level, as the `safe` choice; a fallback to the height map
  if neither path can cope.
- **Live walk and rewind.** A block journal: a contribution (move index, value) is stored per block, the
  rewind is a binary search and a `max` over the contributions, with no full recompute of the prefix.
- **Accuracy.** The shape is analytic (a sphere, a cone, a torus as primitives); there is no discretisation
  of a move, both the "`cell/4` band" and the dependence of the shape on the grid step disappear.

### 5.3 Work plan

**Stage 0. Reconnaissance by measurement (no code in the tree).** 0.1 A prototype: a 3D texture R32F plus
a texture indirection over the blocks, ray tracing in `CustomMaterial` - whether the OpenGL 3.3 branch and
weak hardware can cope. 0.2 A prototype: marching cubes + dual contouring, a triangle count and the cost.
0.3 Compare both paths, pick the primary and the fallback. Exit criterion: there is a decision and
measurements; the prototype does not enter the tree.

**Stage 1. Core (pure, no Qt and no OCCT).** 1.1 A block grid 16^3, the narrow band, implicit interior and
emptiness. 1.2 The signed field and the swept body: a capsule and a closed-form minimum over `t`;
subtraction `f = max(f, -g)`. 1.3 Surface extraction: marching cubes and dual contouring as a separate
module. 1.4 A multi-dexel as a double. 1.5 A block journal. Checks: a new probe `implicit_stock_probe` - a
flat plunge, a ball, an **undercut** (the main new case), a sphere shell, monotonicity and determinism, a
rewind, an inclined move with a ball against a quadrature. Criterion: the deviation from the analytics is
no more than 1e-6 mm where the field is smooth.

**Stage 2. Drawing.** 2.1 Path R1: adaptive mesh extraction. 2.2 Path R2: a 3D texture, ray tracing, the
normal from the gradient. 2.3 The choice of path by hardware and level, a fallback to the height field.
2.4 Publication of the dirty blocks only. Checks: a `stock_bind_probe` double (the integrity of
publication, no loops and gaps, a comparison of what is drawn with the analytics); field frames in
`bench_probe` (stage 0.1 of plan 1). Criterion: the undercut is readable, there are no steps, the frame is
within the budget.

### 5.4 Cost, risks, what we reject

- **Memory.** The volume field is more expensive than a map. An estimate (proposal): a narrow band 4-6
  voxels wide over an area S at a voxel `h` is on the order of `S / h^2 * band` voxels against
  `area / h^2` for the map; the gain against the full volume is orders of magnitude, but there is a loss
  against the map. The exact numbers are a measurement of stage 0.
- **OpenGL 3.3 and weak hardware.** Ray tracing may not be affordable - then R1 or a fallback to the map.
- **Dependencies.** We take the idea of sparse blocks, we do not pull in the library: a minimal block grid
  is our own code.
- **Surface extraction.** Marching cubes give a voxel-sized staircase; dual contouring removes it but
  requires the gradient (Hermite data).
- **We reject:** a full voxel B-rep and booleans over polygons, FEM and haptics, neural surrogates.

## 6. Plan 3 - the cut as a space-time field

The third variant changes the class of the problem: plan 1 counts from the tool (path-centric), plan 2 -
from the tool in 3D, plan 3 - **from the field (field-centric)**: the cut is the lower envelope of the
tool bodies = a generalized weighted distance transform, and the cut itself is stored as a field in time.

### 6.1 Three pillars

- **The envelope as a distance transform.** A height map is a minimum along the path:
  `H(X, Y) = min over p from the path [ z_tip(p) + h( |(X, Y) - p| ) ]`. For a ball cutter this is the
  signed distance to a set of balls (a true sphere, no 12 chords). For a flat cutter `h(r) = 0` - a
  weighted Voronoi diagram. For a cone - the envelope of cones. It is counted by "first arrival" solvers:
  fast sweeping (Zhao 2005), fast marching (Sethian), jump flooding on the GPU (Rong and Tan, 2006).
  Metaphor: the cut goes as a front. Honestly: jump flooding is approximate, so the authority is fast
  sweeping on the CPU, the GPU is an accelerator with a control cleanup pass; determinism requires fixed
  tie-breaking.
- **The chronicle of the cut.** A node stores the path length `t_cut` at which it was cut; the field at
  moment `t` is the nodes with `t_cut <= t`. The rewind becomes O(1) (today - a full recompute of the
  prefix, 200-232 ms, and at a 0.2 node up to 4 s); playback is a shift of the threshold, on the GPU one
  uniform. The cost is plus one float32 per node. Metaphor: the stock is a 4D object, not a snapshot of
  "now".
- **Progressive delivery.** A frame does not depend on the size of the stock: geometry clipmaps (Losasso
  and Hoppe, 2004), layered depth images (Shade et al., 1998), out-of-core bricks.
- **Addendum: an exact oracle on a CAD kernel and a deviation map.** The swept volume can be built once as
  an honest B-rep body and subtracted with a boolean operation (OCCT is already among the dependencies).
  For thousands of moves it is expensive - so not interactive but a reference: a probe checks the fast
  field against the exact boolean difference. From there a deviation map (remainder against the target
  part) grows.

### 6.2 How it looks as a whole

1. **Seeders.** The path is split into seeder points (no denser than the node) with the weight `z_tip` (for
   a ball - the centres of the balls).
2. **Solver.** The lower envelope by first arrival: fast sweeping on the CPU (authority), jump flooding on
   the GPU (accelerator with a cleanup pass). The output is the height and `t_cut` per node.
3. **Chronicle.** The field and the time side by side; a frame at moment `t` is a threshold by time.
4. **Delivery.** The clipmap cuts the grid by window; on a weak machine - layered depth images; on a large
   stock - out-of-core bricks.
5. **Oracle.** On small programs the exact B-rep difference confirms the field; the deviation map shows the
   remainder against the part.

### 6.3 Work plan

**Stage 0. Reconnaissance by measurement (a prototype).** 0.1 Fast sweeping against the current pass on
`Test_model`: whether the field matched and by what factor it is cheaper. 0.2 Jump flooding on the GPU:
accuracy against the CPU. 0.3 Clipmap and layered depth images: the cost of a frame against the size of
the stock. Exit criterion: there is a solution and numbers; the prototype does not enter the tree.

**Stage 1. Core (pure, without Qt and OCCT).** 1.1 Seeders from the path. 1.2 A first-arrival solver: the
height and `t_cut`. 1.3 Fixed tie-breaking and order of sweeps for determinism. 1.4 The chronicle. Checks:
a new probe `front_stock_probe` - flat, ball, cone; a match with the current `simulateStock`; a rewind as a
threshold equals a pass from the start; determinism when the order changes. Criterion: the deviation from
the analysis is no more than 1e-6 mm; the step band is absent.

**Stage 2. Drawing and delivery.** 2.1 The chronicle in the shader. 2.2 A grid clipmap. 2.3 Layered depth
images as an emergency path. Checks: a `stock_bind_probe` double, a comparison of what is drawn with the
field, field frames.

**Stage 3. Oracle and deviation map.** 3.1 The probe `brep_oracle_probe`: builds the swept volume and
subtracts it from the stock with an OCCT boolean operation, and checks the fast field against the exact
difference. 3.2 A deviation map: the remainder against the target part, numbers in mm as a line. Checks:
the field against the B-rep difference within a tolerance derived from the node; the deviation is
deterministic.

### 6.4 Risks and what we reject

- **A generalized distance for an arbitrary profile.** The exact link "profile -> metric" is proven for a
  ball and a plane; for a chamfer and a cone the metric is anisotropic - derive it or keep the stamping of
  plan 1.
- **Jump flooding is approximate.** Either a cleanup pass to zero errors (JFA+2, 1+JFA), or the GPU for
  preview only. For verification "almost exact" is not good enough.
- **OpenGL 3.3 and weak hardware.** The clipmap and the layered images are fallback paths; a rollback to
  plan 1 or 2.
- **The cost of `t_cut`.** Plus one float32 per node; in the Test_model stock map this is another 236 KiB
  at a 0.5 mm node.
- **We reject:** neural surrogates of the field, full FEM and haptics.

## 7. Plan 4 - screen-space CSG of swept bodies

A second attempt at the task of an exact boundary: the cut boundary must be exact **by construction**, not
obtained from a grid. The work happens on the reserved branch `alt/stock-csg`; the measurements of `main`
are from `cpp/core/quality.h`, the measurements of the exact method from the journal of `alt/stock-sim`.

### 7.1 The method: a BVH of swept bodies and an exact ray

- **The swept body of a move.** `StockProfile` already splits the cutting part into pieces (a line and an
  arc). The body of the move `A -> B` is the envelope of a piece over the segment:

| Piece of `StockProfile` | Body of the move | Primitive |
|---|---|---|
| a line with constant `h` (flat tip, drill) | a cylinder along the segment | cylinder |
| a line with a slope (cone, chamfer) | a truncated cone along the segment | truncated cone |
| an arc with its centre on the axis (ball) | a ball dragged along the segment | capsule |
| an arc with an offset centre (the corner rounding) | a torus along the segment | torus |

- **The BVH.** A tree over the AABBs of the bodies, built in the order of the moves, deterministically. A
  tree node holds its AABB and a range of bodies, a leaf holds a batch of bodies.
- **An exact ray instead of a marching step.** The intersection is found in closed form: the nearest root
  of the ray with a primitive, and the tree walk culls by AABB. There is no marching step, so the picture
  depends neither on the node nor on the scale - the boundary is exact at any zoom. That is the point of
  the method.
- **A hybrid is allowed.** Where a scalar field is enough (floors, the untouched top, the data on the
  removed volume, the comparisons) the field stays; CSG takes what needs exactness. The choice is by view
  (`stockView`), by hardware, by geometry.
- **What CSG gives along the way.** The rewind is free (the bodies are numbered in the order of the moves,
  a prefix is a filter by number). An undercut is expressed by a body (a lollipop is a torus along the
  segment).
- **What needs reconnaissance.** How the tree reaches the shader, how the screen pass writes depth and
  whether it lives with the stock glass and the toolpath ribbons. This is a change of the rendering
  architecture, not a shader edit.

### 7.2 Stages

**Stage 0. Reconnaissance and measurements.** 0.1 A probe of the BVH build over `Test_model.ngc`: the
number of bodies, the build time, the bytes. 0.2 The ceiling of the target hardware: a minimal screen
shader on a 1024x600 frame at 1, 10 and 100 bodies per ray. 0.3 Reconnaissance of passing the tree to the
shader (an R32F/RGBA32F texture against a buffer). 0.4 Reconnaissance of the pass: how the screen removal
writes depth and lives with the glass, the walls, the ribbons, the skin. Criterion: the number of bodies,
the build cost, the frame cost on the target hardware and the path of the tree into the shader are named.

**Stage 1. The CSG core in C++ (branch, `<base>+csg.1`).** 1.1 `core/stock_csg.{h,cpp}`: the bodies of a
move from the pieces of `StockProfile`. 1.2 A BVH over the AABBs, deterministically. 1.3 Ray-primitive
intersection in closed form plus a tree walk. 1.4 The reference is the same minimum by which a field node
is computed. Checks: `csg_ray_flat`, `csg_ray_ball`, `csg_ray_cone` (agree with `sweepExact` to 1e-9 mm),
`csg_boundary_exact`, `csg_undercut`, `csg_build_price`. Criterion: the deviation from the analysis is no
more than 1e-9 mm.

**Stage 2. Integration and a fallback (`<base>+csg.2`).** 2.1 Passing the bodies and the tree to the
shader. 2.2 The screen pass. 2.3 The hybrid: the field where it is enough, CSG where exactness is needed; a
fallback with one handle. 2.4 Live playback and a rewind by the move number. Checks: `stock_bind_probe`,
the field digests unmoved, the view frames, the fallback on and off. Criterion: the picture at a node of
0.5 and 0.05 mm is the same; there is no transition face at a step; the frame cost is within the budget of
stage 0.2.

### 7.3 The price

| What | Number | From |
|---|---|---|
| Moves in `Test_model.ngc` | 70 914 | `quality.h:70` |
| Bodies per program | about 70 000-200 000 | an estimate |
| BVH build | not measured; the order is tens of ms | an estimate |
| Memory of the bodies and the tree | 2.3-6.4 MiB and 4.5-12.8 MiB | an estimate |
| Frame cost on the target hardware | not measured | the GPU ceiling is unknown |

What we do not know and do not guess (the work of stage 0): the GPU ceiling of the target machine, the
cost of the BVH build and its share of the cold load, how the screen pass lives with the layers, how much
of the frame budget is left.

### 7.4 Risks and what we reject

- **Weak hardware.** A full-frame screen march is not for an Atom class machine; CSG is introduced as a
  mode with a fallback to the field, not as the default; the frame cost is measured on the target hardware.
- **The domain where the field is better:** floors and the untouched top, the data on the removed volume
  and the comparisons, the live tick. The hybrid is a rule of choice, not a compromise in words.
- **A marching step instead of a closed-form intersection - rejected:** the accuracy would depend on the
  step.
- **B-rep booleans in OCCT per move - rejected:** good for the final verification of one part.
- **A screen result as a reference in the probes - rejected:** it depends on the frame and the camera; CSG
  is verified by the ray analysis, a frame is the acceptance picture.
- **Determinism.** Screen CSG yields a picture, not a field: the bytes of the field and
  `oracle_manifest.json` do not change.

## 8. Comparison of the plans

| | Plan 1 (height map) | Plan 2 (volume field) | Plan 3 (field and time) | Plan 4 (screen CSG) |
|---|---|---|---|---|
| Direction of the count | from the tool | from the tool, 3D | from the field (first arrival) | from the surface (a ray) |
| Core of the math | a closed envelope of the move | a 3D body subtraction | a distance transform | ray-BVH intersection |
| Undercut | refusal | expressed | expressed | expressed (a body) |
| An axis not along Z | no | expressed | expressed | expressed |
| Chronicle of time | a journal of contributions (option) | a journal of blocks | the main representation | the move number (a prefix) |
| Rewind | a prefix recompute | a journal | O(1), a threshold | a filter by number |
| Boundary | an exact contour from the field | mesh extraction / a ray | mesh extraction / a ray | exact by construction |
| Frame from size | grows with the stock | grows with the band | limited by the window (clipmap) | depends on the number of bodies |
| Memory | over the stock, cheap | over the narrow band, more expensive | plus time per node | bodies + BVH |
| Oracle of accuracy | analysis | analysis + marching cubes | OCCT B-rep | ray analysis |
| Complexity | medium | high | high | high |
| Risk on weak hardware | low | medium | medium (there are rollbacks) | medium (a fallback to the field) |
| Compatibility with the code | reuses `StockField` | a new core next to it | a new core next to it | a change of the rendering architecture |

Recommendation: the plans do not compete but divide the tasks. The near step is plan 1 (accuracy and speed
in the current architecture, fits into weak hardware). Undercuts and multi-axis - plan 2. Scale and time -
plan 3 (the distance transform removes the dependence on the number of moves, the chronicle gives an
instant rewind, the clipmap holds the frame, the B-rep oracle and the deviation map turn the picture into a
program check). Boundary accuracy - plan 4 (as a mode with a fallback to the field). The works are
independent and do not break the bit-exact contract.

## 9. Acceptance, documents, versions (common to the plans)

- The six `--manifest` probes and `oracle_manifest.json` are not touched (the field and the screen render
  are not part of the contract) - confirm them green.
- `DESIGN.md` and `DESIGN.en.md`: extend/rewrite the "Material removal simulation" section in one pass in
  both languages; remove the promise lines without code.
- The viewer's `CHANGELOG.md` (at the top, in Russian: what and why), `VERSION`, `projects.json`; a MINOR
  per stage; the version of an experimental branch is `<base>+<tag>.<n>` (tag `alt`, `csg`).
- Commits `feat(gcode-viewer): ...` separately per stage; the checks per `VERSIONING.md` §5.
- Before/after frames in `img/` - the visual acceptance (the boundary at a zoom, a step, an undercut).

## 10. Open questions

- Whether MRR (volume in cm3 per tool) is needed in the near-term plan or after the `alt/stock-sim` branch
  merges.
- The GPU ceiling on the target machine (Intel Atom + Nvidia Ion): how many triangles, tile textures and
  bodies per ray a 1024x600 frame carries - the measurement of stage 0 decides.
- Whether sparse blocks (plan 2) are enough for undercuts or dexels at once.
- The metric for a chamfer and a cone in plan 3: an exact anisotropic one or the stamping of plan 1.
- The accuracy of jump flooding: whether a cleanup pass to zero errors is needed on the live branch.
- The capacity of `t_cut`: a plain float32 or packing the time.
- How the CSG tree reaches the shader: as a texture (the project's path) or as a buffer.
- Whether the field stays the source of data (the removed volume, MRR) and the only answer on weak hardware
  after the merge; whether CSG is needed in live playback or only on pause and on a frame.
