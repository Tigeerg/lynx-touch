// The arms of the Q5 kinds (PLAN-QUICKPROG 7.6): the thread mill, the engraving and the pocket with
// an island.
//
// The counterbore is a drilling layout with a seat, so its arm stands with the drilling ones in
// `ops-kinds.ts`; the thread mill is another shape of a hole, the engraving is a text and the island
// pocket is a second rectangle inside the first, and their arms live here so the union file stays
// under the size guide. `ops.ts` re-exports all of them, so a caller says `ThreadOperation` as it
// says any other kind.
import type { Circle, OperationBase, Rect } from './ops'

/**
 * A thread mill (`thread`, PLAN-QUICKPROG 7.6): a helix of one pitch per turn around one hole;
 * `hole.r` is the crest inside or the flank outside, the helix turns on `hole.r -/+ D/2`.
 */
export interface ThreadOperation extends OperationBase {
  kind: 'thread'
  /** The thread: the centre and the radius of the crest (internal) or the flank (external). */
  hole: Circle
  /** The thread pitch, mm per turn: the climb of the helix per revolution. */
  pitchMm: number
  /** True for a nut (the mill inside the hole), false for a stud (outside). */
  internal: boolean
  /** The length of the thread along Z, mm; the turns are `threadDepthMm / pitchMm`. */
  threadDepthMm: number
  /** The stock left by the rough passes, mm; the last pass is the finish. */
  threadAllowanceMm: number
  /** The step between two radial passes, % of the diameter. */
  stepOverPct: number
}

/**
 * A single-stroke text (`engrave`, PLAN-QUICKPROG 7.6): the built-in font cuts the string on the
 * surface of the top, so `depthMm` is how deep the mill sinks into the top of the material. The
 * glyph table lives in `engrave-font.ts`; a character outside it is refused by the check.
 */
export interface EngraveOperation extends OperationBase {
  kind: 'engrave'
  /** The string to cut; the built-in set is the digits, the Latin and the Cyrillic capitals. */
  text: string
  /** The centre of the string along X and its baseline along Y, mm. */
  x: number
  y: number
  /** The cap height of the letters, mm; the width follows the font and this height. */
  heightMm: number
  /** The turn of the string around `(x, y)`, degrees, counter-clockwise. */
  angleDeg: number
}

/**
 * A pocket with an island (`pocket-island`, PLAN-QUICKPROG 7.6): the raster of a rectangular pocket
 * with the island cut out of it, then a finish pass around the island at the allowance distance. The
 * island stands inside the pocket with a clearance of at least the mill diameter between the two.
 */
export interface IslandPocketOperation extends OperationBase {
  kind: 'pocket-island'
  /** The pocket: the rectangle the raster covers. */
  rect: Rect
  /** The island: the rectangle the raster does not enter and the finish pass walks around. */
  island: Rect
  /** The stock left on the island beyond the mill radius, mm. */
  islandAllowanceMm: number
  /** The step between two rows of the raster, % of the diameter. */
  stepOverPct: number
}
