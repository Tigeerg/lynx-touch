// The arms of the Q5 kinds (PLAN-QUICKPROG 7.6): the thread mill.
//
// The counterbore is a drilling layout with a seat, so its arm stands with the drilling ones in
// `ops-kinds.ts`; the thread mill is another shape of a hole, and its arm lives here so the union
// file stays under the size guide. `ops.ts` re-exports both, so a caller says `ThreadOperation` as
// it says any other kind.
import type { Circle, OperationBase } from './ops'

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
