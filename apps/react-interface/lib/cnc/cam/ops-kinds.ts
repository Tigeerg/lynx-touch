// The arms of the operation union, one per kind (PLAN-QUICKPROG, 6 and 7.4).
//
// A kind lives here together with the geometry and the parameters that exist only for it, so a form
// or a planner that forgets a kind does not compile. The common fields of every kind sit in
// `OperationBase` and the shapes of the whole document in `ops.ts`, which re-exports this module.
// This is a leaf of the type graph: it imports types only (`DrillBody` straight from `feeds.ts`, so
// `ops.ts` re-exports this module and not the other way), no value of the logic.
import type { DrillBody } from './feeds'
import type { Circle, OperationBase, Point3, Rect } from './ops'

/** Face milling of the stock top: strips across the stock at `stepOverPct` of the diameter. */
export interface FaceOperation extends OperationBase {
  kind: 'face'
  /** The plane to cut, not always the stock top: the operator may take a light pass. */
  z: number
  stepOverPct: number
}

/** Rectangular pocket; the raster does not cut the corner radius, so the corner keeps the mill. */
export interface RectPocketOperation extends OperationBase {
  kind: 'pocket'
  rect: Rect
  cornerRadiusMm: number
  /** Stock left on the walls only; the floor is cut to the final depth in the same pass. */
  finishAllowanceMm: number
  stepOverPct: number
}

/** Round pocket cut by a spiral; the allowance is the wall stock, the step is radial. */
export interface CirclePocketOperation extends OperationBase {
  kind: 'circle-pocket'
  circle: Circle
  finishAllowanceMm: number
  stepOverPct: number
}

/** A single hole at the centre; the radius of `hole` is the drill radius. */
export interface DrillHoleGeometry {
  pattern: 'hole'
  hole: Circle
}

/** `cols` x `rows` holes from (x0, y0), row by row along X. */
export interface DrillGridGeometry {
  pattern: 'grid'
  x0: number
  y0: number
  cols: number
  rows: number
  dx: number
  dy: number
}

/** `count` holes on a circle: the first at `startDeg`, clockwise, a full turn or `arcDeg`. */
export interface DrillRingGeometry {
  pattern: 'circle'
  x: number
  y: number
  count: number
  r: number
  startDeg: number
  arcDeg: number
  fullTurn: boolean
}

export type DrillGeometry = DrillHoleGeometry | DrillGridGeometry | DrillRingGeometry

/** The hole layout of one drilling operation: one hole, a grid or a ring. */
export type DrillPattern = DrillGeometry['pattern']

/**
 * Drilling: one kind, three layouts. `peckMm` above zero selects G83 with that Q, zero leaves the
 * cycle to the planner; `body` is the drill material and selects the row of the table.
 */
export interface DrillOperation extends OperationBase {
  kind: 'drill'
  pattern: DrillPattern
  body: DrillBody
  peckMm: number
  /**
   * The R plane of the cycle, mm, and whether the document set it. A zero is not "R at the top"
   * but "the default" (one millimetre above the top, clamped to safe Z), the same way a zero peck
   * is not a peck. Both are part of the `.qprog` (`op.r`) and of the demo model.
   */
  rMm: number
  hasR: boolean
  /** The single hole: `hole` is the centre and the drill radius. The ring uses `hole.x/y`. */
  hole: Circle
  x0: number
  y0: number
  cols: number
  rows: number
  dx: number
  dy: number
  count: number
  startDeg: number
  arcDeg: number
  fullTurn: boolean
}

/** Contour along a polyline in XY (PLAN-QUICKPROG, 7.6): the centre walks `D/2 + allowanceMm` off the
 * profile, on the material side or outside; a closed contour returns to its first point, an open one
 * leads in and out. The `z` of a point is unused.
 */
export interface ContourOperation extends OperationBase {
  kind: 'contour'
  /** The profile vertices in XY, in the order of the walk; `closed` adds the last segment. */
  points: Point3[]
  closed: boolean
  /** Stock left on the profile beyond the mill radius, mm; zero cuts to size. */
  allowanceMm: number
  /** `material` keeps the mill inside the profile, `air` keeps it outside. */
  side: 'material' | 'air'
}

/**
 * A straight keyway (PLAN-QUICKPROG, 7.6): the mill cuts from `from` to `to` along one straight, and
 * `widthMm` is the width of the slot across the walk. A slot as wide as the mill is one pass; a wider
 * one is several passes across, each `stepOverPct` of the diameter apart.
 */
export interface SlotOperation extends OperationBase {
  kind: 'slot'
  from: Point3
  to: Point3
  /** The width of the slot across the walk, mm; between `D` and `2D`. */
  widthMm: number
  /** The step between the strips across the slot, % of the diameter, as of a pocket. */
  stepOverPct: number
}

/**
 * A chamfer along one straight edge (PLAN-QUICKPROG, 7.6): the mill runs `from` -> `to`, sunk
 * `chamferMm` below the top, so its corner takes the edge off. Only a straight edge is modelled;
 * `grain` names the direction of the edge against the part and is kept for a later planner, the cut
 * itself does not read it yet.
 */
export interface ChamferOperation extends OperationBase {
  kind: 'chamfer'
  from: Point3
  to: Point3
  /** The leg of the chamfer, mm; not above the mill radius. */
  chamferMm: number
  /** The edge against the grain of the part; the direction is not planned yet. */
  grain?: 'along' | 'across'
}
