// Tapping (PLAN-QUICKPROG 7.6): the G84 cycle, its feed, its planes and its time.
//
// A rigid tap turns one pitch per spindle revolution, so the feed of the cycle is `pitch x rpm`,
// mm per minute: the advance per minute = the advance per turn x the turns per minute. The control
// keeps the spindle and the Z axis in step, and the return is the same feed in reverse, so the time
// of one hole is out and back, `2 * depth / feed`; a tap does not peck, so there is no Q and no
// rapid hop to charge.
//
// The cycle is a machine feature like G81/G83, so the planner draws only the rapids to the centres
// (`tap-holes.ts`) and the emitter writes `G84 X Y Z R F` and the closing `G80`. R is the same
// plane the drill uses, a millimetre above the material top clamped to safe Z.
import type { Operation, QuickProgram, TapOperation } from './ops'
import { tapHoles } from './tap-holes'
import type { PathBuilder } from './path-builder'

/** The R plane above the material top when the document does not set it, mm (the drill's rule). */
const RISE_MM = 1

/** The planes and the feed of a tapping cycle, what the emitter and the time estimate read. */
export interface TapCycle {
  /** The Z of the hole bottom, mm. */
  depthZ: number
  /** The R plane, mm. */
  rZ: number
  /** The plunge feed of G84, mm/min: `pitch x rpm`. */
  feedMmMin: number
}

/** Everything the emitter and the time estimate read of one tapping operation. */
export interface TapPlan {
  points: { x: number; y: number }[]
  /** The layout errors in Russian; a layout with errors plans nothing. */
  errors: string[]
  cycle: TapCycle
}

const asTap = (op: Operation): TapOperation => {
  if (op.kind !== 'tap') throw new Error(`planTap called with a ${op.kind} operation`)
  return op
}

/**
 * The feed of G84: the advance of the tool per spindle turn, mm/min. A tap is rigid, so this feed
 * is not the cutting feed of the table: the spindle and the Z axis are the same drive, and the
 * number is fixed by the pitch. A zero pitch or speed gives zero, which the tap check refuses first.
 */
export const tapFeedMmMin = (op: TapOperation): number => op.pitchMm * op.cutting.spindleRpm

/** The cycle of a tap: the bottom, the R plane and the feed of the rigid tap. */
export function tapCycle(program: QuickProgram, op: TapOperation): TapCycle {
  const top = op.topZ
  const rZ = Math.max(Math.min(top + RISE_MM, program.safeZ), top)
  return { depthZ: top - op.depthMm, rZ, feedMmMin: tapFeedMmMin(op) }
}

/** The tapped centres, or the layout errors. */
export function tapPlan(program: QuickProgram, operation: Operation): TapPlan {
  const op = asTap(operation)
  const layout = tapHoles(op)
  return { points: layout.points, errors: layout.errors, cycle: tapCycle(program, op) }
}

/** The rapids of a tap to each centre at safe Z, as of a drill; the emitter writes the cycle. */
export function planTap(program: QuickProgram, operation: Operation, out: PathBuilder): TapPlan {
  const plan = tapPlan(program, operation)
  for (const hole of plan.points) out.add('rapid', { x: hole.x, y: hole.y, z: program.safeZ })
  return plan
}

/**
 * The tapping time of the holes, minutes: the tap goes down and comes back at the same feed, so one
 * hole costs `2 * depth / feed`. The reversal at the bottom is the machine's, not a dwell.
 */
export function tapCycleMinutes(op: TapOperation, points: number): number {
  const feed = tapFeedMmMin(op)
  if (feed <= 0) return 0
  return (points * 2 * op.depthMm) / feed
}
