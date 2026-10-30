// The shape values of the Q5 kinds (PLAN-QUICKPROG 7.6) in both directions of the form.
//
// A counterbore carries the drilling layout and its seat; a thread carries the hole, the pitch, the
// side and the two numbers of the helix. The switch of the form (`form-fields-kinds.ts`) calls
// these two functions, so the arms that hold the layout of a fastener and a thread do not push the
// switch over the size guide.
import type { CounterboreOperation, Operation, ThreadOperation } from './ops'
import type { CommonFields } from './form-fields'
import { kept, int, on, str } from './form-fields'

/** A counterbore: the drilling layout plus the seat; the form starts on one hole. */
type Counterbore = Extract<Operation, { kind: 'counterbore' }>
type Thread = Extract<Operation, { kind: 'thread' }>

/** The layout values a counterbore shares with a drill, as the form shows them. */
const layoutValues = (op: Counterbore): Record<string, string> => ({
  pattern: op.pattern,
  'hole.x': String(op.hole.x),
  'hole.y': String(op.hole.y),
  'hole.r': String(op.hole.r),
  x0: String(op.x0),
  y0: String(op.y0),
  cols: String(op.cols),
  rows: String(op.rows),
  dx: String(op.dx),
  dy: String(op.dy),
  count: String(op.count),
  startDeg: String(op.startDeg),
  arcDeg: String(op.arcDeg),
  fullTurn: op.fullTurn ? 'on' : 'off',
})

/** The layout values of a counterbore written over the fields of `op`. */
const layoutFields = (op: Counterbore, v: Record<string, string>): Counterbore => ({
  ...op,
  pattern: str(v, 'pattern', op.pattern) as Counterbore['pattern'],
  hole: {
    x: kept(v, 'hole.x', op.hole.x),
    y: kept(v, 'hole.y', op.hole.y),
    r: kept(v, 'hole.r', op.hole.r),
  },
  x0: kept(v, 'x0', op.x0),
  y0: kept(v, 'y0', op.y0),
  cols: int(v, 'cols', op.cols),
  rows: int(v, 'rows', op.rows),
  dx: kept(v, 'dx', op.dx),
  dy: kept(v, 'dy', op.dy),
  count: int(v, 'count', op.count),
  startDeg: kept(v, 'startDeg', op.startDeg),
  arcDeg: kept(v, 'arcDeg', op.arcDeg),
  fullTurn: on(v, 'fullTurn', op.fullTurn),
})

/** The shape values of a counterbore: the hole layout and the seat of the fastener head. */
export function counterboreValues(op: CounterboreOperation): Record<string, string> {
  return {
    ...layoutValues(op),
    seatDiameterMm: String(op.seatDiameterMm),
    seatDepthMm: String(op.seatDepthMm),
    dwellSec: String(op.dwellSec),
  }
}

/** The shape values of a thread: the hole, the pitch, the side and the helix numbers. */
export function threadValues(op: ThreadOperation): Record<string, string> {
  return {
    'hole.x': String(op.hole.x),
    'hole.y': String(op.hole.y),
    'hole.r': String(op.hole.r),
    pitchMm: String(op.pitchMm),
    internal: op.internal ? 'on' : 'off',
    threadDepthMm: String(op.threadDepthMm),
    threadAllowanceMm: String(op.threadAllowanceMm),
    stepOverPct: String(op.stepOverPct),
  }
}

/**
 * The typed fields of a counterbore written over the common ones of the operation. The common
 * fields replace the operation's (`common` holds the kind), the shape fields stay the op's.
 */
export function counterboreFields(
  op: CounterboreOperation,
  v: Record<string, string>,
  common: CommonFields,
): Counterbore {
  const layout = layoutFields(op, v)
  return {
    ...op,
    ...layout,
    ...common,
    kind: 'counterbore',
    seatDiameterMm: kept(v, 'seatDiameterMm', op.seatDiameterMm),
    seatDepthMm: kept(v, 'seatDepthMm', op.seatDepthMm),
    dwellSec: kept(v, 'dwellSec', op.dwellSec),
  }
}

/** The typed fields of a thread written over the common ones of the operation. */
export function threadFields(op: ThreadOperation, v: Record<string, string>, common: CommonFields): Thread {
  return {
    ...common,
    kind: 'thread',
    hole: {
      x: kept(v, 'hole.x', op.hole.x),
      y: kept(v, 'hole.y', op.hole.y),
      r: kept(v, 'hole.r', op.hole.r),
    },
    pitchMm: kept(v, 'pitchMm', op.pitchMm),
    internal: on(v, 'internal', op.internal),
    threadDepthMm: kept(v, 'threadDepthMm', op.threadDepthMm),
    threadAllowanceMm: kept(v, 'threadAllowanceMm', op.threadAllowanceMm),
    stepOverPct: kept(v, 'stepOverPct', op.stepOverPct),
  }
}
