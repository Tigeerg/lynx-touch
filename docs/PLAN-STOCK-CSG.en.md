# Material removal simulation: screen-space CSG of swept bodies - work plan

A working document, not a description of what has been done. A second attempt at one task: the cut
boundary must be exact **by construction**, not obtained from a grid. The source of truth for the
current code is `cpp/core/stock_sim.{h,cpp}` (the `StockProfile` table and the profile pieces),
`qmlbackend/gv_stockgeom.*`, the shaders `stocksurface.*`, `stockshell.*`, the boundary layers
`stockcutcontour.*` and `stockcutwall.*`, and the "Material removal simulation" section in
`DESIGN.md`. The numbers here are either taken from measurements (with the source named) or labelled
as estimates.

Where the numbers of the exact method come from. The exact sweep (`StockField::sweepExact`) and its
boundary live on the experimental branch `alt/stock-sim`; its code and its records are not in `main`,
where only the sampling of positions exists (`StockField::sweep`, step `cell / 2`). The measurements
of the exact method are therefore given by reference to the branch journal:
`modules/gcode-viewer/CHANGELOG.md` on the branch `alt/stock-sim`, records `0.58.1+alt.1`, `+alt.4`,
`+alt.5`. The measurements of `main` are from `cpp/core/quality.h`.

The order: reconnaissance and measurements first (stage 0, without touching the tree), then the core
with analytic cases (stage 1), then the viewer integration with a fallback to the field (stage 2),
then acceptance and versions (stage 3). The general rules are `CLAUDE.md` (Russian for comments, `->`
instead of an arrow glyph, units in names) and `VERSIONING.md` (the order of editing, the checks, the
versions; the version of an experimental branch is `<base>+<tag>.<n>`, the tag here is `csg`). The
place reserved for the code is the branch `alt/stock-csg` (branched from `main`, it has no records of
its own yet).

## 1. What exists now and where it loses

- The representation is a **height map**: at a node of the machine X/Y grid one depth from the top of
  the stock (float32 -> R32F), the node sits at the corner of the extent. The removal is `sweepExact`:
  the minimum over `t` from `[0, 1]` of `z(t) + h(r(t))` along the analytic envelope `StockProfile`
  (line pieces and arc pieces), exactly the capsule of the move is walked, each node once per move
  (record `0.58.1+alt.1`).
- The cut boundary has **its own representation, independent of the threshold of the field** (record
  `0.58.1+alt.4`): marching squares over the signs of the nodes picks candidate edges, and the
  position of the crossing on an edge is refined by bisection over the exact analytic depth
  (`sweepPointDepth`, the same formula as at a field node). The signed distance
  `stockCutSignedDistance` (an R32F texture) carries the colour and the line; the vertical wall and
  the polyline are built by `stockCutWall` and `stockCutContour`.
- The top of the field **between nodes stays bilinear**: at a step the vertex takes the level of its
  own side (`steppedDepth`, record `0.58.1+alt.5`), but the height inside a cell is an interpolation
  of nodes.

Measurements (values and their source):

| What | Number | Source |
|------|--------|--------|
| Boundary error by the field threshold: wall / ball, node 0.5 mm | 0.475 / 0.223 mm | `0.58.1+alt.4`, cases `boundary_wall`, `boundary_ball` |
| The same, node 0.2 mm | 0.190 / 0.044 mm | same |
| The same, node 0.1 mm | 0.095 / 0.0073 mm | same |
| The exact boundary (refined crossing) | 1.5e-8 / 6.7e-9 mm | same |
| The drawn boundary (zero level of the signed distance) | 0.011 mm at node 0.5; 0.002 at 0.2 | same, case `boundary_drawn` |
| The boundary polyline is a chord between crossings | 0.010 mm at node 0.5 and radius 3 | same |
| Tilt of the transition face from the vertical at a step of 0.8 / 4.0 mm, node 0.5 mm | 17.4 / 3.6 degrees | `0.58.1+alt.5`, case `island_rim_drawn` |
| Divergence of the drawing from the field at a bilinear cell | up to a quarter of the cell twist | `DESIGN.md`, "Material removal simulation" |
| The same in mm at a step of 4 mm and a node of 0.5 mm | up to 1.0 mm | an estimate from the rule above: the cell twist reaches the step |
| Cost of the exact sweep of the whole program, node 0.5 / 0.2 mm | 118.1 / 790.6 ms (sampling of positions - 391.2 / 5666.0) | `0.58.1+alt.1` |
| Cost of the boundary: full assembly / live tick | 160 ms (node 0.5) / 1.1-2.4 ms per 0.1-0.5 % of the path | `0.58.1+alt.4` |
| Accuracy of the sweep against the analysis: ball / cone | 1.93e-8 / 1.21e-8 mm (the 12 chords of the table gave 8.35e-3) | `0.58.1+alt.1`, `+alt.2` |

Hence where it loses:

1. **The boundary is a property of the field, not of the surface.** A node decides which edge is
   chosen, a linear interpolation decides the position of the crossing and the signed distance: the
   drawing diverges from the field by a quarter of the cell twist, and at the edge itself the height
   between nodes is not exact. A finer grid does not solve the task: a node of 0.1 mm gives 0.095 mm
   by the threshold and 6 MiB against 236 KiB, and 0.05 mm is four times more expensive again
   (record `0.58.1+alt.4`).
2. **A step is drawn by a sub-cell.** The vertical edge is carried by the wall layer, and at the top
   of the field a transition face remains: 17.4 degrees from the vertical at a step of 0.8 mm (record
   `0.58.1+alt.5`). "No steps" at a zoom runs into the sub-cell (cell / `stockSubdiv`), not into a
   rule.
3. **The accuracy depends on the node and on the view**, although the task is geometric: the same cut
   at a node of 0.5 and at 0.05 mm is a different picture, and between them there is only memory and
   time.
4. **An undercut cannot be expressed by a map at all**: the walk refuses entirely, there is no field
   (`StockCutter::undercut`, record `0.49.0`). That is the limit of the representation, not an error
   of the node.

## 2. The method: a BVH of swept bodies and an exact ray

- **The swept body of a move.** `StockProfile` already splits the cutting part into pieces: a line in
  the `(r, h)` plane and an arc. The body of the move `A -> B` is the envelope of a piece over the
  segment. Nothing new has to be computed: these are the same pieces with which `sweepExact` takes
  the minimum.

| Piece of `StockProfile` | Body of the move | Primitive |
|---|---|---|
| a line with constant `h` (flat tip, drill) | a cylinder along the segment | cylinder |
| a line with a slope (cone, chamfer) | a truncated cone along the segment | truncated cone |
| an arc with its centre on the axis (ball) | a ball dragged along the segment | capsule |
| an arc with an offset centre (the corner rounding) | a torus along the segment | torus |

- **The BVH.** A tree over the AABBs of the bodies, built in the order of the moves, deterministically:
  the same order of the program, without threads and reorderings, as in the field. A tree node holds
  its AABB and a range of bodies, a leaf holds a batch of bodies.
- **An exact ray instead of a marching step.** The eye ray is reconstructed from the screen position,
  and the intersection is found **in closed form**: the nearest root of the ray with a primitive, and
  the tree walk culls by AABB. The removal is a subtraction: the surface is where the ray entered the
  stock and did not leave inside the union of the bodies. There is no marching step, so the picture
  depends neither on the node (there is no grid at all) nor on the scale: the boundary is exact at any
  zoom and at any cell size - that is the point of the method.
- **A hybrid is allowed and expected.** Where a scalar field is enough (floors, the untouched top, the
  data on the removed volume, the comparisons) the field stays; CSG takes what needs exactness: the
  cut boundary, a pocket wall, a step, an undercut. The choice is by view (`stockView`), by hardware
  (a fallback on a weak GPU) and by geometry (a body above a floor is not marched at all).
- **What CSG gives along the way.** The rewind is free: the bodies are numbered in the order of the
  moves, and any prefix is a filter by number. An undercut is expressed by a body (a lollipop is a
  torus along the segment) where the map refuses entirely.
- **What needs reconnaissance, not a promise.** How the tree reaches the shader (the project already
  has the path of an R32F texture through `QQuick3DTextureData`; there is no buffer path in the
  project's shaders), how the screen pass writes depth (at present only a depth shift in the vertex
  shader, `stockDepthOffset` -3e-5 NDC, `DESIGN.md`) and whether it lives with the stock glass and the
  toolpath ribbons. This is a change of the rendering architecture, not a shader edit
  (`PLAN-STOCK-METHODS.md`, section 3).

## 3. Stages

### Stage 0. Reconnaissance and measurements (without touching the tree)

The work happens on the reserved branch `alt/stock-csg`; nothing comes from this stage into `main`
except the numbers of this document.

- 0.1 A probe of the BVH build over the real program (`Test_model.ngc`, 70 914 moves): the number of
  bodies, the build time of one program, the bytes of the bodies and of the tree.
- 0.2 The ceiling of the target hardware: a minimal screen shader (a ray against the bodies plus a
  tree walk) on a 1024x600 frame at 1, 10 and 100 bodies per ray - ms per frame, on a desktop machine
  and on the target.
- 0.3 Reconnaissance of passing the tree to the shader: an R32F/RGBA32F texture through
  `QQuick3DTextureData` (the project's path) against a buffer, if one is available in Qt 6.12 on the
  OpenGL 3.3 branch.
- 0.4 Reconnaissance of the pass: how the screen removal writes depth and how it lives with the stock
  glass (`stockGhost` 0.90, depth write `AlwaysDepthDraw`), the walls, the toolpath ribbons, the skin.
  Reference frames of the current field - the visual acceptance.
- Checks: the numbers of 0.1-0.2 are written into the document and into a probe comment; the answers
  of 0.3-0.4 are named outright, together with what did not work.
- Criterion: the number of bodies and the build cost are named, so are the frame cost on the target
  hardware and the path of the tree into the shader. Until that answer, no code goes into `main`.

### Stage 1. The CSG core in C++ and analytic cases

Branch `alt/stock-csg`, version `<base>+csg.1`.

- 1.1 `core/stock_csg.{h,cpp}`: the bodies of a move from the pieces of `StockProfile` (cylinder,
  truncated cone, capsule, torus). A clean layer, like `core/stock_sim`: no Qt, no OCCT.
- 1.2 A BVH over the AABBs of the bodies: range bounds, build order, the number of bodies per leaf.
  Deterministic.
- 1.3 Ray-primitive intersection in closed form (truncated cone, torus, capsule) plus a tree walk with
  AABB culling; the nearest root.
- 1.4 The reference is the same minimum by which a field node is computed (`sweepExact` /
  `sweepPointDepth`): one formula for a node and for a ray point.
- Checks: the probe cases `csg_ray_flat`, `csg_ray_ball`, `csg_ray_cone` (the ray and `sweepExact`
  agree to 1e-9 mm), `csg_boundary_exact` (the boundary matches the analysis to 1e-9 mm),
  `csg_undercut` (a lollipop: the body removes metal under the lip, the map refuses), `csg_build_price`
  (the build numbers).
- Criterion: the deviation from the analysis is no more than 1e-9 mm, the cost of the build and of the
  memory is named.

### Stage 2. Viewer integration and a fallback to the field

Version `<base>+csg.2`.

- 2.1 Passing the bodies and the tree to the shader by the path of stage 0.3; publication as with the
  field, as whole data.
- 2.2 The screen pass: ray reconstruction, tree walk, subtraction; depth and ordering per stage 0.4, so
  that the toolpath ribbons and the skin hide behind the removed metal.
- 2.3 The hybrid: the field where it is enough, CSG where exactness is needed; a switch and a fallback
  to the field with one handle (weak hardware, a shader refusal).
- 2.4 Live playback and a rewind by the move number (the bodies are a prefix of the path); the field
  stays for the data on the removed volume.
- Checks: `stock_bind_probe` (the binding of the layers to the trajectory), the field digests unmoved,
  the view frames in `img/`, the fallback on and off.
- Criterion: the picture at a node of 0.5 and at 0.05 mm is the same (the boundary does not depend on
  the node); there is no transition face at a step; the frame cost on the target hardware is within the
  budget named by stage 0.2.

### Stage 3. Acceptance, documents, versions

- 3.1 The six `--manifest` probes and `oracle_manifest.json` are not touched (the field and the screen
  render are not part of the contract) - confirm them green.
- 3.2 `DESIGN.md` and `DESIGN.en.md`: extend the "Material removal simulation" section with the CSG
  mode in one pass in both languages; remove the promise lines without code.
- 3.3 The viewer journal, `modules/gcode-viewer/VERSION`, `projects.json` - per `VERSIONING.md`; the
  branch version is `<base>+csg.<n>`, the records of the branch go into `main` as they are.
- 3.4 Before/after frames in `img/`: the boundary at a zoom, a step, an undercut.
- 3.5 Commits `feat(gcode-viewer): ...` separately per stage; the checks per `VERSIONING.md` §5.

## 4. The price

The numbers below are by measurement where a source is named, and an estimate where they are an order.

| What | Number | From |
|------|--------|------|
| Moves in `Test_model.ngc` | 70 914 | `cpp/core/quality.h:70` (a measurement) |
| Pixels of a 1024x600 frame | 614 400 | `DESIGN.md` |
| Bodies per move | 1-3 by the number of `StockProfile` pieces (flat, drill - 1; ball - 1; ball with a chamfer - 2-3) | an estimate from the code |
| Bodies per program | about 70 000-200 000 | an estimate |
| BVH build, one per program | not measured; the order is tens of ms (for comparison: the full exact sweep at a node of 0.5 - 118.1 ms) | an estimate; record `0.58.1+alt.1` |
| Memory of the bodies and the tree | 2.3-6.4 MiB and 4.5-12.8 MiB | an estimate |
| The field (stays in the hybrid) | 236 KiB at a node of 0.5, a full assembly 192.4 ms | `cpp/core/quality.h:70-78` |
| Frame and pixel cost on the target hardware | not measured | the GPU ceiling of the target machine is unknown |
| The boundary today, for comparison: field threshold / drawing | 0.475 / 0.223 mm at a node of 0.5; 0.010 mm for the polyline | records `0.58.1+alt.4`, `+alt.5` |

What we do **not know** and do not guess (that is the work of stage 0):

- the GPU ceiling of the target machine: how many bodies per ray a 1024x600 frame carries and how many
  of them are reachable in one tree walk;
- the cost of the BVH build over the real program and its share of the cold load;
- how the screen pass lives with the existing layers: the stock glass, the depth write
  `AlwaysDepthDraw`, the depth shift `stockDepthOffset` -3e-5 NDC, the toolpath ribbons, the skin;
- how much of the frame budget is left after what the scene already takes;
- how the tree reaches the shader on the OpenGL 3.3 branch (see 0.3).

## 5. Risks and what we reject

- **Weak hardware.** A full-frame screen march is not for a target machine of the Atom class
  (`PLAN-STOCK-METHODS.md`, section 3). CSG is therefore introduced as a **mode with a fallback to the
  field**, not as the default, and the frame cost is measured on the target hardware (stage 0.2), not
  on a desktop machine.
- **The domain where the field is better and stays:** floors and the untouched top (a plane, nothing to
  march), the data on the removed volume and the comparisons (field digests), the live tick (the field
  is 1.1-2.4 ms incrementally, record `0.58.1+alt.4`; CSG pays per frame, every frame). The hybrid is
  not a compromise in words but a rule of choice.
- **An undercut** is the domain where CSG is stronger than the map (a body expresses a lollipop and a
  dovetail), but until stage 1 that is an experiment, not a proof.
- **A marching step instead of a closed-form intersection - rejected:** the accuracy would depend on
  the step, and "exact by construction" would stay a word. The ray is intersected with a primitive
  analytically.
- **B-rep booleans in OCCT per move - rejected:** seconds to minutes per operation
  (`PLAN-STOCK-METHODS.md`, section 3). Good for the final verification of one part, not for a
  simulation.
- **A screen result as a reference in the probes - rejected:** it depends on the frame, the resolution
  and the camera. CSG is verified by the ray analysis; a frame serves as the acceptance picture
  (`img/`).
- **Antialiasing.** The boundary is geometrically exact but is drawn per pixel, so the zigzag of the
  raster remains; it is treated by the same means as now - the distance to the boundary. "Exact by
  construction" is about geometry, not about the raster, and that is named rather than implied.
- **Determinism.** Screen CSG yields a picture, not a field: the bytes of the field and
  `oracle_manifest.json` do not change. If CSG ever replaces the field in the comparisons, that is a
  separate decision with an explicit entry in `CHANGELOG.md`.

## 6. Open questions

- The GPU ceiling of the target machine and the admissible number of bodies per ray - the measurement
  of stage 0.2.
- How the tree reaches the shader: as a texture (the project's path) or as a buffer, if one is
  available in Qt 6.12 on the OpenGL 3.3 branch - the reconnaissance of stage 0.3.
- Whether CSG is needed in live playback or only on pause and on a frame - the measurement of stage 0.2.
- Whether the field stays the source of data (the removed volume, MRR) and the only answer on weak
  hardware after the merge.
- Whether the method is needed at all, if stage 0 shows that the target hardware does not carry the
  frame: then CSG stays a mode of evidential frame verification (`PLAN-STOCK-METHODS.md`, section 4,
  item 6).
