// The hole centres of a tap (PLAN-QUICKPROG 7.6), laid out by the drilling geometry.
//
// A tap has no layout of its own: the grid and the ring are the drilling ones, so the tap is read as
// a drill of the same layout and `drillHoles` lays it out, exactly as the drill and the check do. This module keeps that one bridge, so the planner, the checker and the emitter
// cannot drift apart on the centres of a tapped grid.
import type { CounterboreOperation, DrillOperation, TapOperation } from './ops'
import type { HoleLayout } from './drill-holes'
import { drillHoles } from './drill-holes'

/** The tap as a drill of the same layout: `drillHoles` reads only the layout fields copied here. */
export const tapAsDrill = (op: TapOperation): DrillOperation => {
  // The two kinds share the layout fields by construction (`ops-kinds.ts`); the peck, the body and
  // the R plane of a drill are not read by `drillHoles`, so the defaults here never reach a file.
  const base: DrillOperation = {
    ...op,
    kind: 'drill',
    body: 'hss',
    peckMm: 0,
    rMm: 0,
    hasR: false,
  }
  return { ...base, ...geometryOf(op) }
}

/** The layout fields of a tap, in the drill spelling the layout functions read. */
const geometryOf = (
  op: TapOperation,
): Pick<
  DrillOperation,
  'hole' | 'x0' | 'y0' | 'cols' | 'rows' | 'dx' | 'dy' | 'count' | 'startDeg' | 'arcDeg' | 'fullTurn'
> => ({
  hole: op.hole,
  x0: op.x0,
  y0: op.y0,
  cols: op.cols,
  rows: op.rows,
  dx: op.dx,
  dy: op.dy,
  count: op.count,
  startDeg: op.startDeg,
  arcDeg: op.arcDeg,
  fullTurn: op.fullTurn,
})

/** The tapped centres, or the layout errors in Russian; an error gives no points. */
export const tapHoles = (op: TapOperation): HoleLayout => drillHoles(tapAsDrill(op))

/** The seat centres of a counterbore: the same bridge, the same drilling layout. */
export const counterboreHoles = (op: CounterboreOperation): HoleLayout => drillHoles(op)
