// The defaults of the Q5 kinds (PLAN-QUICKPROG 7.6): a fastener seat, a thread mill, an engraving
// and a pocket with an island.
//
// A counterbore opens on the drilling frame with a hole and a seat of the M6 head: the hole is the
// tool of the form, the seat is wider than it and the seat depth starts shallow. A thread mill opens
// on an M8x1.25 internal thread of the tool: the radius follows the tool until the operator types
// the thread, and the rough pass leaves a tenth of a millimetre for the finish.
//
// The constants live here, so `defaults.ts` imports them back for the export list and the two long
// arms of the switch stay out of the switch of the older kinds.
import type {
  CounterboreOperation,
  EngraveOperation,
  IslandPocketOperation,
  Material,
  RestOperation,
  ThreadOperation,
  ToolRef,
} from './ops'
import type { NewOperation } from './operation-init'
import { common } from './defaults-common'

/** The seat of a new counterbore and its dwell: the head of an M6 screw in a 6 mm hole, G82 P0.2. */
export const DEFAULT_SEAT_DIAMETER_MM = 11

/** The dwell of G82 of a new counterbore, seconds: the console value. */
export const DEFAULT_DWELL_SEC = 0.2

/** The seat depth of a new counterbore, mm: the M6 head is 6 mm, the form starts shallow. */
export const DEFAULT_SEAT_DEPTH_MM = 6

/**
 * The R plane of a new counterbore above the material top, mm. The plane is required: the check
 * refuses a seat without it, so the form opens on a valid request among the drill RISE_MM of the
 * plan (`path-counterbore.ts`). The field R itself is not in the form yet.
 */
export const DEFAULT_COUNTERBORE_R_MM = 1

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
    rMm: DEFAULT_COUNTERBORE_R_MM,
    hasR: true,
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

/** The height of a new engraving, mm: a readable label of the console size. */
export const DEFAULT_ENGRAVE_HEIGHT_MM = 5

/** The allowance of a new island pocket on the island, mm: the rough pass leaves a tenth. */
export const DEFAULT_ISLAND_ALLOWANCE_MM = 0.1

/**
 * A new engraving: the string `ТЕКСТ` over the operation point, 5 mm high and level. The text is a
 * placeholder the operator replaces; every character of it is in the built-in set.
 */
export function newEngrave(init: NewOperation, tool: ToolRef, material: Material): EngraveOperation {
  const fields = { ...common(init, tool, material, null), stepDownMm: 0 }
  return {
    ...fields,
    kind: 'engrave',
    text: 'ТЕКСТ',
    x: 0,
    y: 0,
    heightMm: DEFAULT_ENGRAVE_HEIGHT_MM,
    angleDeg: 0,
  }
}

/** A new island pocket: the 60 x 40 pocket of the console and a 20 x 20 island in its centre. */
export function newIslandPocket(init: NewOperation, tool: ToolRef, material: Material): IslandPocketOperation {
  const fields = common(init, tool, material, null)
  return {
    ...fields,
    kind: 'pocket-island',
    rect: { x: -30, y: -20, w: 60, h: 40 },
    island: { x: -10, y: -10, w: 20, h: 20 },
    islandAllowanceMm: DEFAULT_ISLAND_ALLOWANCE_MM,
    stepOverPct: THREAD_STEP_OVER_PCT,
  }
}

/**
 * A new rest operation: no source (the whole footprint, with the warning the check prints) and no
 * footprint of its own, so the form opens on "the whole stock" and the operator narrows it.
 */
export function newRest(init: NewOperation, tool: ToolRef, material: Material): RestOperation {
  const fields = common(init, tool, material, null)
  return {
    ...fields,
    kind: 'rest',
    source: [],
    rect: { x: 0, y: 0, w: 0, h: 0 },
    allowanceMm: 0,
    stepOverPct: THREAD_STEP_OVER_PCT,
  }
}
