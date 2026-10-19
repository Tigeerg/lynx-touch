# Material removal simulation, the second option: an implicit volume model

A working document next to `PLAN-STOCK.md`. The first plan is a height map: a layered
surface with an exact contour and an exact envelope of a move over a capsule. The second
plan is a different class of representation, borrowed from industrial NC verification and
from medical visualisation: a **volume implicit field** (an implicit material function),
from which the surface is extracted separately. The source of truth for the current code is
`cpp/core/stock_sim.{h,cpp}`, `stocksurface.vert/frag`, `qmlbackend/gv_*`, `DESIGN.md`
(the section "Material removal simulation"). The numbers here are either a measurement or
named as a proposal. The rules are `CLAUDE.md` and `VERSIONING.md`.

## 1. Why the second option is needed at all

A height map (the current one, and the first plan) by definition stores one height per
column. So it **does not express an undercut** — a lollipop, a dovetail, a T-slot — and
**does not express an axis not along Z** (4-5 axes); today the viewer honestly refuses to
compute such programs at all. That is no small thing: undercuts and multi-axis work are
exactly what advanced machining does, and what industrial NC verification systems show,
while a height map does not.

A volume field removes both limitations: the material is a region in 3D, the tool subtracts
from it its swept body (the volume traced by the motion), and the cuts appear by themselves,
with no agreement that "the axis is only Z". The price is memory and complexity, so this is
a separate, bolder plan rather than a replacement of the first one.

## 2. Where the techniques come from

Nothing is invented; every node of the method has a pedigree.

- **Dexel, multi-dexel, three-directional dexel.** A dexel is a "depth pixel", a node of the
  height field; in the surface machining literature it is taken as a multilevel Z-map, and
  to express undercuts one moves to **three orthogonal dexel grids** (along Z, X and Y) and
  to a **multi-interval** dexel (an ordered list of Z intervals per column). The techniques:
  Choi and Jerard, "Sculptured surface machining" (1998); the three-directional dexel — Gao,
  Zhang, Hou (2007); morphology on a multi-dexel — Zhao and Qian (2009). This is what NC
  verification in applied CAM is usually built on.
- **Level sets and signed distance fields (SDF).** The surface is defined by the zero level
  of a function `f(xyz)`, inside `f > 0`, outside `f < 0` (Osher and Sethian; in medicine —
  segmentation and boundary tracking). The SDF gradient equals one almost everywhere (the
  eikonal equation) and on the boundary it gives the normal — hence even shading and an exact
  silhouette. A fast distance field is computed by fast marching and fast sweeping (Zhao
  2005).
- **Marching cubes.** Surface extraction from a volume scalar field; invented for medical CT
  and MRI visualisation (Lorensen and Cline, 1987), the two-dimensional version is marching
  squares. Dual contouring (Ju, Losasso, Schaefer, Warren, 2002) builds a mesh from Hermite
  data (value and gradient) and **preserves sharp edges** — what is needed for a pocket wall
  and a chamfer produced by a cutter.
- **Sparse volume blocks and the narrow band.** Not the whole volume is stored, but the
  blocks near the surface (the narrow band); sparse volume libraries (VDB) are built this
  way. We take the idea, we do not pull in the dependency: a minimal block grid is our own
  code.
- **Truncated signed distance field (TSDF).** A distance field stored only in a narrow band
  around the surface (as in volume reconstruction, KinectFusion, Izadi et al., 2011) —
  exactly what is needed: the material is interesting only at the boundary, the depth inside
  the stock carries no shape.
- **Ray tracing over the field (sphere tracing / ray marching).** The surface is drawn from
  the SDF with no mesh at all, with an exact silhouette and screen resolution; described in
  Real-Time Rendering. This is the extreme point of "exact drawing": no triangles, no steps,
  no mosaic.

The direct difference from the first plan: there the height map is drawn as a mesh and the
precision is gained by a careful contour; here there may be no surface mesh at all, and the
shape lives in the volume.

## 3. The method

### 3.1 Representation: sparse blocks of the narrow band

- The stock space is divided into **blocks** (for example 16x16x16 voxels). A block that does
  not cross the material boundary (deep inside or far outside) is not stored: its value is
  implicit (solid material or emptiness). Only the blocks of the **narrow band** near the
  surface are stored — that is the saving.
- In a narrow-band voxel there is a signed distance field `f`: `f > 0` is material, `f < 0`
  is emptiness (removed material), `f = 0` is the surface. The sign stores the topology,
  undercuts included.
- Next to it there is a secondary, cheap field: a **multi-dexel** (a list of Z intervals per
  column). It does not carry the full 3D shape, but it gives a fast recompute and a cheap
  fallback drawing path, like today's height map.

### 3.2 Removal: subtracting the swept body

- The tool is a solid of revolution; its body swept over a move is the volume traced by the
  motion. Subtraction from the material in implicit form: `f_new = max(f_old, -g)`, where `g`
  is the signed distance to the swept body (inside the body `g < 0`). Material outside the
  body is untouched, inside it is removed.
- The move body for a sphere is a capsule (a segment plus a radius) with a closed-form
  distance; for a general profile (`h(r)`) it is a closed-form minimum over `t` (the same
  convexity as in the first plan, but the distance is taken in 3D, not only along Z).
- The `max` runs over all moves; the field is **monotone** and does not depend on the order,
  so a live walk and a rewind can be done the same way as now, and bitwise comparisons stay
  meaningful.
- A cheap double on the multi-dexel: subtracting the swept body as an operation of
  **mathematical morphology** over the intervals of a column (erosion of the interval by the
  body) — fast, and it expresses an undercut, because a column may have several intervals.

### 3.3 Drawing: two paths, both more exact than the current mesh

- **Path R1, mesh extraction (medical).** Marching cubes over `f = 0` give triangles; dual
  contouring over the gradient (`grad f`) preserves the sharp cutting edges. The mesh is
  adaptive (we subdivide only near the surface and near the edges) and fits the existing
  `QQuick3DGeometry` contract — QML barely changes. An untouched stock is a few triangles,
  not millions.
- **Path R2, without a mesh (ray tracing).** The field as a 3D texture with an indirection
  texture over the blocks; the surface is found by ray tracing over the SDF. An exact
  silhouette, smoothing by derivatives, screen resolution rather than mesh resolution; the
  normal comes from the gradient on the spot. No steps, no mosaic, no `NORMAL_SPAN`.
- The choice of path is by hardware and quality level, as `safe` is chosen today: a weak
  OpenGL branch and a software rasteriser take R1 or, if even that cannot cope, a fallback to
  the height field (the first plan).

### 3.4 Live walk, rewind, memory

- A block journal: the removal is monotone, so a contribution can be stored per block (move
  index, value) — a rewind becomes a binary search and a `max` over the contributions, with
  no full recompute of the prefix (today that is 200-232 ms, and at a 0.2 node up to 4 s).
- Memory: it is paid only for the narrow-band blocks; deep material and emptiness are
  implicit. The estimate is below, and it is honestly named a proposal, not a measurement.

### 3.5 Accuracy

- The shape is analytic, not faceted: a sphere, a cone, a torus as primitives; marching cubes
  and dual contouring reconstruct the surface from the value and the gradient, so a cutting
  edge stays sharp and a spherical valley stays smooth (unlike the 12 chords of today's
  table, a sagitta of 0.00835 mm).
- There is no discretisation of a move at all: the move body is subtracted in closed form,
  not by sampling positions. Both the "`cell/4` band" and the dependence of the shape on the
  grid step disappear — the grid now sets only the accuracy of surface extraction, not the
  shape of the cut.

## 4. Work plan

### Stage 0. Reconnaissance by measurement (no code in the tree)

- 0.1 A prototype: a 3D texture R32F plus a texture indirection over the blocks, ray tracing
  in `CustomMaterial` — whether the OpenGL 3.3 branch and weak hardware can cope; frame
  numbers. `DESIGN.md` directly requires a prototype before the code.
- 0.2 A prototype: marching cubes + dual contouring over the same field, a triangle count and
  the extraction cost on `Test_model`.
- 0.3 Compare both paths by frame, memory and quality; pick the primary and the fallback.
- Exit criterion: there is a decision and measurements; the prototype does not enter the tree.

### Stage 1. Core: the volume field and subtraction (pure, no Qt and no OCCT)

- 1.1 A block grid: 16^3 blocks, sparse storage, the narrow band, implicit interior and
  emptiness (`cpp/core/`).
- 1.2 The signed field and the swept body of a move: a capsule (sphere) and a closed-form
  minimum over `t` for a general profile; subtraction `f = max(f, -g)`.
- 1.3 The surface extraction steps: marching cubes (topology) and dual contouring (sharp
  edges) as a separate pure module.
- 1.4 A multi-dexel as a cheap double and a fallback path.
- 1.5 A block journal for the rewind.
- Checks: a new probe `implicit_stock_probe` with analytics computed not by the field code:
  a flat plunge (material where the tool passed), a ball (a true sphere), an **undercut** (a
  lollipop leaves an overhang — where a height map would have cut it off; that is the main new
  case), a sphere shell (sign and distance), monotonicity and determinism (a bitwise digest
  with a different order of moves), a rewind (a prefix digest). An inclined move with a ball —
  a comparison against a high-accuracy quadrature.
- Criterion: the deviation from the analytics is no more than 1e-6 mm where the field is
  smooth.

### Stage 2. Drawing

- 2.1 Path R1: adaptive mesh extraction, a joint with `QQuick3DGeometry`, the same materials
  and colours; an untouched stock is single-digit triangles.
- 2.2 Path R2: a 3D texture, ray tracing, the normal from the gradient, silhouette AA;
  quality thresholds (the number of steps, the ray resolution).
- 2.3 The choice of path by hardware and level, like the choice of `safe`; a fallback to the
  height field if neither path can cope.
- 2.4 Publication: only dirty blocks are updated (a 3D texture has no partial upload — the
  blocks are published by their own textures or in full over the dirty set).
- Checks: a `stock_bind_probe` double for the new field — the integrity of publication (during
  a recompute the previous field in full), the absence of loops and gaps in the extracted
  surface (the Euler characteristic, the boundary as closed contours), a comparison of the
  drawn surface with the analytics (the bottom under the tip at the height of the tip, the
  undercut overhang at its own height). Field frames in `bench_probe` (stage 0.1 of
  `PLAN-STOCK.md`).
- Criterion: the undercut is readable, there are no steps, the frame is within the orbit
  budget.

### Stage 3. Acceptance, documents, versions

- 3.1 `oracle_manifest.json` and the six `--manifest` probes are not touched; the field does
  not enter the contract.
- 3.2 `DESIGN.md` and `DESIGN.en.md`: extend the section "Material removal simulation" with
  the second representation (the height map as the fast path, the volume field for undercuts
  and multi-axis work), in one pass in both languages.
- 3.3 `modules/gcode-viewer/CHANGELOG.md`, `VERSION`, `projects.json`; MINOR for each stage.
- 3.4 Commits: `feat(gcode-viewer): ...` per stage; checks per `VERSIONING.md` §5.

## 5. Cost, risks, what we reject

- **Memory.** The volume field is more expensive than a height map. An estimate (proposal): a
  narrow band 4-6 voxels wide over a surface of area S at a voxel `h` is on the order of
  `S / h^2 * band` voxels against `stock area / h^2` for the map; the gain against the full
  volume is orders of magnitude, but there is a loss against the height map, and it is paid
  only where the shape in the volume is needed. The exact numbers are a measurement of
  stage 0.
- **The OpenGL 3.3 branch and weak hardware.** Ray tracing over a 3D texture may not be
  affordable — then R1 (the mesh) or a fallback to the height map; that is a named fallback,
  not a surprise.
- **Dependencies.** We take the idea of sparse blocks, we do not pull in the library: a
  minimal block grid is our own code, as the repository style requires (no new dependencies
  silently).
- **Surface extraction.** Marching cubes give a voxel-sized staircase; dual contouring
  removes it, but it requires the gradient. If the gradient is not enough (the field is
  stepped at an edge), the edge returns as a staircase — it is cured by Hermite data (the
  value and the gradient in a voxel), which is the essence of dual contouring.
- **We reject:** a full voxel B-rep and booleans over polygons (expensive and beyond the
  task), FEM and haptics (not about drawing the removal), neural-network surrogates
  (unverifiable and non-portable).

## 6. Comparison of the two plans

| | Plan 1 (height map) | Plan 2 (volume field) |
|---|---|---|
| Representation | one height per column | a signed material field in the volume |
| Undercut | a refusal (today) | expressed |
| An axis not along Z | no | expressed |
| Shape accuracy | an exact envelope along Z | analytic in 3D |
| Memory | over the stock, cheap | over the narrow band, more expensive |
| Drawing | a layered mesh + contour | a mesh (dual contouring) or a ray |
| Complexity | medium | high |
| Hardware | fits into OpenGL 3.3 | the ray path is in question, there is a fallback |
| Compatibility with the current code | reuses `StockField` | a new core next to it |

Recommendation: the first plan is the nearest step (accuracy and speed within the current
architecture, guaranteed to fit into weak hardware); the second plan is strategic, for
undercuts and multi-axis work, and it is reasonable to launch it as a superstructure: the
height map stays the fast path, the volume field is enabled where the program cannot be
expressed by a map. Both works are independent and do not break the bit-exact contract.

## 7. Open questions

- The primary drawing path: a ray (R2) is more exact, but there is a risk on weak hardware; a
  mesh (R1) is safer. The measurement of stage 0 decides.
- The narrow-band voxel and the band width: a proposal of 0.2-0.5 mm and 4-6 voxels, refined
  by memory on `Test_model` (extent 150x100x40 mm).
- Whether an English mirror file (`PLAN-STOCK-IMPLICIT.en.md`) is needed before the transfer
  into `DESIGN.md`, or whether the mirror is enough at stage 3.2.
