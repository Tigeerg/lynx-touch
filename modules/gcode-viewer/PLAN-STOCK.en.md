# Material removal simulation: work plan

A working document, not a description of what has been done. The source of truth for the current code is
`cpp/core/stock_sim.{h,cpp}`, `stocksurface.vert/frag`, `qmlbackend/gv_stockgeom.*`,
`qmlbackend/gv_toolpath.*` and the "Material removal simulation" section in `DESIGN.md`. The numbers
here are either taken from measurements (with the source named) or labelled as proposals.

The order: first exact rendering (stage 1) on the current field, then a mathematically fast and exact
computation of the field (stage 2). The general rules are `CLAUDE.md` (Russian for comments, `->`
instead of an arrow glyph, units in names) and `VERSIONING.md` (the order of editing, the checks, the
versions).

## 1. What exists now

- Representation: a height map nx x ny over machine X/Y, at a node the depth from the top of the
  stock (float32 -> R32F). The node index is `iy * nx + ix`.
- Removal: `StockField::plunge` at one position, `StockField::sweep` walks the 3D length of the
  segment with a step of `cellMm * 0.5`, the field takes the `max` of the depth (that is, the `min`
  of the height). The lower envelope of the profile `h(r)` cuts up to the stick-out; the holder does
  not cut.
- Profile: `StockCutter` is a radial table `h(r)` with linear interpolation; the ball is sampled
  with 12 chords, the corner chamfer with 8.
- Undercut (`LOLLIPOP`, `DOVETAIL`, `T_SLOT`/`TSLOT`, and any profile wider at the top): the walk
  refuses entirely, there is no field at all.
- Live walk: `StockWalk` keeps the field and the cursor, continues from the last whole move, and on
  a step back along the path resets and walks from the start.
- Rendering: one vertex per node, two triangles per cell, the whole grid; the vertex shader
  subtracts the height with a nearest fetch; the normal is a central difference over
  `NORMAL_SPAN = 2` texels; the colour is a threshold `cutMm = 0.05` on `vDepth`.
- Publication: a copy of the whole field into a `QByteArray` on the worker thread, a whole upload of
  `QQuick3DTextureData` once per 100 ms (it has no partial upload).

Measurements of `Test_model.ngc` (70 914 moves, 137.8 m, `UI_STOCK` extent 150x100 mm),
`cpp/core/quality.h:70-78`:

| node | nodes | cost | field | triangles (grid) |
|------|-------|------|-------|------------------|
| 0.5 mm | 301x201 = 60 501 | 192.4-425 ms | 236 KiB | about 120 000 |
| 0.2 mm | 751x501 = 376 251 | 2288.6-8393 ms | 1.44 MiB | about 750 000 |

The cap per `DESIGN.md` is up to 2 093 000 triangles (node 0.2, grid cap 1024).

## 2. Where precision and time are lost

1. A node walked again and again. A node on the trajectory is covered by roughly `4r / node`
   neighbouring positions of one move (at `r = 3`, node 0.2 -> up to 120 times), and every position
   recomputes `sqrt`, `heightAt` and `max`. The main cost multiplier.
2. Discretisation of the move. The step `cell / 2` approximates the envelope: outside the radius an
   uncut strip up to `cell / 4` wide is left. The `flat_sweep` case in `stock_sim_probe` is forced
   to exclude that strip from the comparison.
3. Chords instead of arcs. The sagitta of the ball's 12 chords is 0.00835 mm (`stock_sim_probe`,
   case `ball_plunge`); the chamfer arc is coarser.
4. The whole grid. Memory, triangles and the texture upload are counted over the entire stock
   rectangle, although only a thin strip or a pocket is touched.
5. Steps in the rendering. A piecewise-constant height, a normal over 2 texels, a binary colour by
   nearest: mosaic and jagged edges; `NORMAL_SPAN` is a compromise between the slope of the edge and
   the loss of small windows (`stocksurface.vert`).
6. `DESIGN.md` promises frame measurements of the field (the frame lines of `bench_probe`), but
   `cpp/bench_probe.cpp` has none - the frame budget of the field is not measured at all.

## 3. The new method

The surface is assembled from layers with an exact cut boundary, and the removal is computed as the
exact envelope of each move over a capsule, not as a sampling of positions. The storage is the same
nodes, but sparse (tiles), with a "touched" mask next to the depth.

### 3.1 Rendering

- A layered surface instead of one extruded grid: the top (the plane `zMax` where nothing is
  touched), the bottom of the cut (the height map) and a vertical wall along the exact contour of the
  cut boundary.
- The exact contour is marching squares: the depth at the boundary crosses zero linearly, the
  sub-cell position of the crossing gives a polyline, and from it the vertical quads. A pocket wall
  becomes a straight line, without steps.
- Continuous reconstruction of the height in the shader (by hand from 4-16 texels, not with a
  filter: linear filtering of R32F is not guaranteed on the OpenGL 3.3 branch). The normal is
  computed in the fragment (Sobel or analytic), `NORMAL_SPAN` goes away.
- An adaptive mesh (restricted quadtree): a cell is subdivided only where the depth changes faster
  than the tolerance; neighbours differ by no more than one level, T-junctions are stitched. The
  frame cost is the perimeter of the cuts, not the area of the stock.
- Tiles instead of one texture: its own `QQuick3DTextureData` and its own `Model` per tile (for
  example, 64x64 nodes). A tick loads only the dirty tiles; an untouched tile is not drawn and not
  loaded.
- Boundary antialiasing by the signed distance to the contour (in the style of the existing
  distance-field AA of the ribbons), without MSAA.

### 3.2 Computation

- The exact envelope of a move. The move `A -> B`, the tool `h(r)`. For a node `P`:
  `H(P) = min over t from [0,1] of ( z(t) + h(r(t)) )`, where `z(t) = zA + (zB - zA) * t`, and
  `r(t)` is the distance from `P` to a point of the segment in XY. Only nodes with
  `min_t r(t) <= maxRadius` are touched, that is the capsule (the segment plus a disc); exactly its
  bbox is walked, each node once per move.
- A closed form instead of iterations. `h(r)` is the lower envelope, non-decreasing and piecewise
  linear. On a piece `h = a + b * r` (`b >= 0`) the function `f(t) = z(t) + b * r(t)` is convex
  (r(t) is convex, b >= 0). The critical point:
  `t = t* - dz * d / ( sqrt(L2) * sqrt(b * b * L2 - dz * dz) )` when `b * b * L2 > dz * dz`,
  otherwise the minimum is at an end; here `t*` is the projection of `P` onto the line of the move,
  `d` is the distance from `P` to that line, `L2 = |B - A|^2`, `dz = zB - zA`. We clamp to the
  sub-interval of the piece (where `r(t)` is in `[r_i, r_i+1]`) and to `[0,1]`; we take the minimum
  of the candidate and the ends. For arcs (sphere, chamfer) `h` is non-decreasing and convex, the
  convexity of `f` is preserved - the minimum is found by a safeguarded Newton with a fixed number
  of steps (deterministic). The result has no discretisation error: both the `cell / 4` strip and
  the dependence on the step go away.
- An analytic profile: primitives (line, arc, torus) next to the existing table; a true sphere and
  chamfer instead of a polygon.
- Sparse tiles: an untouched tile is implicit (depth 0), memory and cost follow the area and the
  perimeter of the cut. Determinism is preserved: the `max` of the depth is exact and does not
  depend on the walk order, so the bitwise digests of the probes are meaningful.
- Live walk: the `StockWalk` cursor stays, the increment goes over the dirty tiles. The rewind is
  through a contribution log: the removal is monotone, a contribution (move index, value) is stored
  per tile, and the rewind is a binary search plus a `max` over the contributions (O(1) per node
  instead of a full recompute of the prefix, which currently costs 200-232 ms, and on a 0.2 node up
  to 4 s per `DESIGN.md`). The baseline version is a recompute over the dirty tiles, the log is a
  separate step.
- Undercut (option): instead of a refusal, a truncated dexel, up to `k` layers per column only in
  the stages with an undercut. `k = 1` is the current map, `k = infinity` is the dexel from
  `DESIGN.md` §13; the cost is paid only where it is needed.
- GPU (option, later): the moves in an SSBO, a gather-min over the capsule in the fragment for
  large programs; on the OpenGL 3.3 branch and weak hardware - after the CPU version.

The estimated gain (an order, not a measurement): removing the repeated walk gives about
`4r / node` (10x at `r = 1.5`, node 0.5; up to 120x at `r = 3`, node 0.2); the sparsity adds a gain
over the share of the touched area.

## 4. Work plan

### Stage 0. Baseline and budget (0.53.0, not functional)

- 0.1 `cpp/bench_probe.cpp`: add the phases and the frames of the field (`stock_frame_on`,
  `stock_frame_off`) - close the divergence from `DESIGN.md`.
- 0.2 `cpp/stock_sim_probe.cpp`: take a baseline and write its numbers (nodes, ms, triangles,
  bytes) into a comment.
- 0.3 A reference screenshot of the current picture in `img/` - the visual acceptance.
- Checks: `pnpm guard`, `bench_probe`, `stock_sim_probe`.

### Stage 1. Exact rendering

- 1.1 `stocksurface.vert/frag`: continuous reconstruction of the height, the normal in the fragment,
  remove the `NORMAL_SPAN` compromise.
- 1.2 `core/stock_sim.{h,cpp}` and `qmlbackend/gv_stockgeom.*`: a layered assembly
  (`top` + `bottom` + a vertical `wall`) and the contour via marching squares.
- 1.3 An adaptive mesh (restricted quadtree) with T-junction stitching; a limit by the triangle
  budget instead of the grid cap.
- 1.4 Tile storage and publication: the tiles of the field, separate `QQuick3DTextureData` and
  `Model`, the upload of the dirty tiles only.
- 1.5 Boundary AA by the signed distance; tone and colour from the reconstructed normal.
- Checks: the `stocksurface_exact` case in `stock_sim_probe` (the wall matches the analytic radius
  boundary to sub-cell precision; the top/wall/bottom joint without a gap); `stock_bind_probe` - the
  integrity of the tile publication (during a tick the previous field in full); the field frames
  from 0.1; a before/after screenshot in `img/`.
- Criterion: no steps on the wall (the "colour transitions per row" metric from `stocksurface.frag`
  is about 1), fewer triangles, a frame no more expensive.

### Stage 2. Mathematically fast and exact computation

- 2.1 `core/tooling.cpp`: an analytic profile (line, arc, torus) next to the table; keep the
  `ProfilePoint` contract for the reference, a new type for the removal.
- 2.2 `core/stock_sim.cpp`: replace the `sweep` sampling with a walk of the capsule and the exact
  minimum `H(P)`; remove the step `cell / 2`.
- 2.3 Sparse tiles: storage, dirty tiles, an implicit untouched top.
- 2.4 A live walk over the dirty tiles; a rewind through the contribution log (option 2.4b, if the
  baseline rewind stays expensive).
- 2.5 (option) a truncated dexel for undercuts instead of a refusal.
- 2.6 (option, later) GPU gather-min.
- Checks: rewrite the cases `flat_sweep` (the comparison is now exact, there is no strip of the
  step), `ball_plunge` (a true sphere, the tolerance from the analysis), `cone_plunge`,
  `near_miss_step` (an exact edge instead of "caught by the step"), `stock_bind`, `stock_live` (the
  live digest equals the full one, the rewind equals the prefix), `stock_undercut`. Add an analytic
  case for a slanted move with a ball - a comparison with a high-precision quadrature over `t`.
- Criterion: the cost of `high` 0.2 mm falls by an order of magnitude and more; the deviation from
  the quadrature is no more than 1e-6 mm instead of 1e-2.

### Stage 3. Acceptance, documents, versions

- 3.1 The six `--manifest` probes and `oracle_manifest.json` are not touched (the field is not part
  of the contract) - confirm them green.
- 3.2 `DESIGN.md` and `DESIGN.en.md`: rewrite the "Material removal simulation" section in one pass
  in both languages; remove the promise lines without code.
- 3.3 The viewer's `CHANGELOG.md` (at the top, in Russian: what and why),
  `modules/gcode-viewer/VERSION`, `projects.json`; a MINOR per stage.
- 3.4 The viewer's `README.md`, if the removal section is there.
- 3.5 Commits: `feat(gcode-viewer): ...` separately per stage; the checks per `VERSIONING.md` §5.

## 5. Risks and what we reject

- The OpenGL 3.3 branch: linear filtering of R32F is not guaranteed - reconstruction by hand; the
  tile textures are verified by a prototype before the code (a measurement, as `DESIGN.md`
  requires).
- Tiles against a single texture: more scene objects; the threshold is by the frame measurement.
- Marching squares gives the boundary by linear interpolation: on a 0.5 mm node it is much more
  precise than the node, but not an absolute arc; if needed - second-order curves per cell.
- We reject for now: a full voxel/SDF-raymarch (memory and OpenGL 3.3), CSG booleans on the GPU
  (not portable to weak hardware), the dexel by default (expensive, `DESIGN.md` §13).
- Determinism: the exact method changes the bytes of the field, but the probe digests compare runs
  with each other, not with a stored reference, so `oracle_manifest.json` is not broken; the effect
  is noted in `CHANGELOG.md` explicitly.

## 6. Open questions

- Whether an English mirror file (`PLAN-STOCK.en.md`) is needed before the plan moves into
  `DESIGN.md`, or whether the mirror at stage 3.2 is enough.
- The frame budget of the field on weak hardware (Intel Atom + Nvidia Ion): how many triangles and
  how many tile textures it carries - decided by the stage 0.1 measurement.
