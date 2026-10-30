// The defaults of the Q5 kinds (PLAN-QUICKPROG 7.6): a fastener seat and a thread mill.
//
// A counterbore opens on the drilling frame with a hole and a seat of the M6 head: the hole is the
// tool of the form, the seat is wider than it and the seat depth starts shallow. A thread mill opens
// on an M8x1.25 internal thread of the tool: the radius follows the tool until the operator types
// the thread, and the rough pass leaves a tenth of a millimetre for the finish.
//
// The constants live here, so `defaults.ts` imports them back for the export list and the two long
// arms of the switch stay out of the switch of the older kinds.
import type { CounterboreOperation, Material, ThreadOperation, ToolRef } from './ops'
import type { NewOperation } from './operation-init'
import { common } from './defaults-common'

/** The seat of a new counterbore and its dwell: the head of an M6 screw in a 6 mm hole, G82 P0.2. */
export const DEFAULT_SEAT_DIAMETER_MM = 11

/** The dwell of G82 of a new counterbore, seconds: the console value. */
export const DEFAULT_DWELL_SEC = 0.2

/** The seat depth of a new counterbore, mm: the M6 head is 6 mm, the form starts shallow. */
export const DEFAULT_SEAT_DEPTH_MM = 6

/** The thread of a new thread mill: the M8 coarse pitch of a tap, internal. */
export const DEFAULT_THREAD_PITCH_MM = 1.25

/** The stock of a new thread on the thread circle, mm: the finish removes it. */
export const DEFAULT_THREAD_ALLOWANCE_MM = 0.1

/** The step of the thread passes and the face strips, % of the diameter. */
const THREAD_STEP_OVER_PCT = 70

/** A new counterbore: the drilling layout and the seat of the fastener head. */
export function newCounterbore(init: NewOperation, tool: ToolRef, material: Material): CounterboreOperation {
  const fields = { ...common(init, tool, material, 'hss'), stepDownMm: 0 }
  return {
    ...fields,
    kind: 'counterbore',
    pattern: 'hole',
    hole: { x: 0, y: 0, r: 3 },
    x0: 0,
    y0: 0,
    cols: 1,
    rows: 1,
    dx: 0,
    dy: 0,
    count: 1,
    startDeg: 0,
    arcDeg: 360,
    fullTurn: true,
    seatDiameterMm: DEFAULT_SEAT_DIAMETER_MM,
    seatDepthMm: Math.min(DEFAULT_SEAT_DEPTH_MM, fields.depthMm),
    dwellSec: DEFAULT_DWELL_SEC,
    rMm: 0,
    hasR: false,
  }
}

/** A new thread mill: an M8 pitch on the thread radius of the tool, internal, one finish pass. */
export function newThread(init: NewOperation, tool: ToolRef, material: Material): ThreadOperation {
  const fields = { ...common(init, tool, material, null), stepDownMm: 0 }
  return {
    ...fields,
    kind: 'thread',
    hole: { x: 0, y: 0, r: (tool.diameterMm > 0 ? tool.diameterMm : 8) / 2 },
    pitchMm: DEFAULT_THREAD_PITCH_MM,
    internal: true,
    threadDepthMm: fields.depthMm,
    threadAllowanceMm: DEFAULT_THREAD_ALLOWANCE_MM,
    stepOverPct: THREAD_STEP_OVER_PCT,
  }
}
