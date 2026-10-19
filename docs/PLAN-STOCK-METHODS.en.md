# Material removal simulation: the voxel and other volumetric methods - an evaluation

A working document, not a description of what has been done. An evaluation of **how to represent the
removed material**: what we take, what we do not take and why. The numbers are either taken from the
project's measurements (with the source named) or labelled as an estimate.

Related documents: `PLAN-STOCK.md` (method 1 - a height map and an exact envelope),
`PLAN-STOCK-IMPLICIT.md` (method 2 - sparse narrow-band blocks),
`PLAN-STOCK-TIMEFIELD.md` (method 3 - the chronicle of the cut and the field as a function of time).

## 1. What exists now

- Representation: a **height map** (2.5D): at a node of the machine grid X/Y one depth from the top
  of the stock, float32 -> R32F. The removal is computed as the **exact envelope of the move**
  (`sweepExact`, entry 0.59.0): every node is visited once per move, with no sampling of positions at
  a half-cell step, an analytic profile (a ball is an arc, not 12 chords).
- Measurements of `Test_model.ngc` (70 914 moves, 137.8 m, `UI_STOCK` extent 150x100x40 mm): node
  0.5 mm - 64 ms and 242 004 B; node 0.2 mm - 443 ms and 1 505 004 B (entry 0.59.0). Accuracy against
  the analysis: a ball 1.93e-8 mm instead of 8.35e-3 with the old sampling.
- **Undercut** (lollipop, dovetail, T-slot) cannot be expressed by a height map - the walk refuses
  entirely, there is no field (entry 0.49.0).
- The **cut boundary** comes from the same field and is therefore quantised by the node: at 160
  columns the edges have 146 joints without a shift, 13 by 1 px (0.4 mm) and 2 by 2 px (entry
  0.61.0). Smoothing by the picture does not cure this - it is a limitation of the data, not of the
  shader.

## 2. The dense voxel: an estimate by the numbers

Stock 150x100x40 mm, one byte per voxel:

| node | voxels | memory | update cost (estimate) |
|------|--------|--------|------------------------|
| 0.5 mm | 300x200x80 = 4.8 million | 4.8 MB | roughly 80 times more expensive than the height map: 64 ms -> about 5 s per pass |
| 0.2 mm | 750x500x200 = 75 million | 75 MB | roughly 1000 times: 443 ms -> minutes |
| 0.1 mm | 12 billion | - | does not fit; a sparse variant is needed |

**The key argument.** Our removal is 3-axis and comes from above, that is, essentially 2.5D: in each
column only the top boundary of the material matters. A height map stores exactly that. A dense voxel
pays for the **whole body** of the tool down to the bottom of the stock - it multiplies memory and
time by the height, without adding information for 3-axis machining.

**It does not save the boundary either:** it is quantised by the voxel size, plus marching cubes on
top - the same step, only for 5 seconds per pass and 75 MB at a 0.2 node. "Ideality" comes not from
the dimensionality of the representation but from a boundary representation of its own.

**Conclusion: we do not take the dense voxel.**

## 3. Other methods - in order of increasing progressiveness

- **Dexels (multi-dexel, tri-dexel).** Several heights per column: 1-2 usually, more at undercuts;
  tri-dexel - three orthogonal grids, to cover all directions. An industry standard (Vericut and the
  like stand on this). Memory grows with the number of layers, not with the volume. **The cheapest
  extension of the height map** and a direct way to lift our refusal at an undercut.
- **Adaptive hierarchical maps** (a quadtree over the 2.5D field, small cells only at the cut
  boundary) - stage 1.3 of plan 1: a smooth boundary for almost the same memory.
- **Narrow-band level set / SDF** (OpenVDB is built this way): the signed distance is stored **only
  in a narrow band at the surface**, the removal is a CSG subtraction of the swept body, the boundary
  is marching cubes at the required frequency. Memory is proportional to the **surface area**, not to
  the volume.
- **Sparse blocks** (method 2): the same, in blocks - convenient for the GPU and for the increment,
  only the blocks under the cutter are touched.
- **Clipmap fields** - a fine grid around the camera and the tool, coarse far away: memory is bounded
  from above, the detail is where one looks. Exactly the "progressive delivery" of method 3.
- **A screen-space CSG tree of swept capsules with a BVH and ray marching.** The shape is not
  discretised at all: every pixel computes the intersection of the ray with the tree of primitives.
  The boundary is **exact by construction** - no steps, no smoothing. The cost is a BVH of tens of
  thousands of capsules and a march per frame: real on a desktop GPU, not on a weak Atom. This is a
  change of the rendering architecture, not an edit of a shader.
- **B-rep booleans on OCCT** per move - exact, but seconds to minutes per operation: good for a final
  check of one part, not for simulation.
- **Analytic volume accounting** (the intersection of the swept body with the remainder above the
  field in closed form) - for MRR and tool life it is more precise and cheaper than any grid.

## 4. What we take, and in what order

1. **The boundary** - a representation of its own, exact and independent of the height map: a fine
   2D field of the contour or an analytic contour of the union of capsules (task 0.62.0). In the
   limit, if ideality under any approximation is needed - the screen-space CSG tree (given a GPU).
2. **Adaptivity and tiles** (stages 1.3 and 1.4 of plan 1): a smooth surface and less memory.
3. **Dexels** - when undercuts are needed: at a small cost, without a change of architecture.
4. **Analytic volume** - when the tool life in cm3 and a deviation map are needed.
5. **Narrow-band level set on the GPU with clipmaps** - when real 4-5-axis geometry and a digital
   twin appear.
6. **Screen-space CSG** - as a means of evidential frame verification, if it is needed.

## 5. What we do not take

- **The dense voxel** - per the numbers of section 2.
- **B-rep booleans per move** - per the cost per operation; final verification only.
- **Depth-peeling as a data source** - it is a picture without data about the removed volume; good
  only for a visual check of hidden surfaces.

## 6. Open questions

- Whether MRR (volume in cm3 per tool) is needed in the near-term plan, or that is after the
  `alt/stock-sim` branch merges.
- What the GPU ceiling on the target machine is: whether screen-space CSG and a narrow band on the
  GPU are acceptable depends on it.
- Whether sparse blocks (method 2) are enough for undercuts, or dexels at once - this is decided by a
  measurement on the first program with an undercut.
