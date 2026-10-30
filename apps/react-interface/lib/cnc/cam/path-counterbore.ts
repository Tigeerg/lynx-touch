// Counterboring (PLAN-QUICKPROG 7.6): the G82 cycle, its dwell, its planes and its time.
//
// G82 is G81 with a dwell `P` at the bottom of the hole: the tool stops at the seat floor for
// `dwellSec` seconds and cleans it, then retracts. The geometry is the drilling one (`drillGeometry`
// via `counterboreHoles`), so a grid or a ring counterbores the same centres it drills, and the
// planner draws only the rapids to those centres: the cycle itself belongs to the machine.
//
// The R plane is the drill's rule, a millimetre above the material top clamped to safe Z: a value of
// the document below the top means "the default", as it does for a drill. The check refuses a seat
// finer than its depth and a missing R, so the plan sees sane numbers.
import type { CounterboreOperation, Operation, QuickProgram } from './ops'
import { counterboreHoles } from './tap-holes'
import type { PathBuilder } from './path-builder'

/** The R plane above the material top when the document does not set it, mm (the drill's rule). */
const RISE_MM = 1

/** The planes, the dwell and the feed of a counterboring cycle, what the emitter reads. */
export interface CounterboreCycle {
  /** The Z of the seat floor, mm. */
  depthZ: number
  /** The R plane, mm. */
  rZ: number
  /** The dwell of G82 at the floor, seconds (the `P` word). */
  dwellSec: number
  feedMmMin: number
}

/** Everything the emitter and the time estimate read of one counterboring operation. */
export interface CounterborePlan {
  points: { x: number; y: number }[]
  /** The layout errors in Russian; a layout with errors plans nothing. */
  errors: string[]
  cycle: CounterboreCycle
}

const asCounterbore = (op: Operation): CounterboreOperation => {
  if (op.kind !== 'counterbore') throw new Error(`planCounterbore called with a ${op.kind} operation`)
  return op
}

/** The cycle of a counterbore: the seat floor, the R plane, the dwell and the feed. */
export function counterboreCycle(program: QuickProgram, op: CounterboreOperation): CounterboreCycle {
  const top = op.topZ
  const defaultR = Math.max(Math.min(top + RISE_MM, program.safeZ), top)
  const asked = op.hasR && op.rMm >= top
  const rZ = asked ? Math.min(op.rMm, Math.max(program.safeZ, top)) : defaultR
  return { depthZ: top - op.depthMm, rZ, dwellSec: op.dwellSec, feedMmMin: op.cutting.feedMmMin }
}

/** The seat centres, or the layout errors. */
export function counterborePlan(program: QuickProgram, operation: Operation): CounterborePlan {
  const op = asCounterbore(operation)
  const layout = counterboreHoles(op)
  return { points: layout.points, errors: layout.errors, cycle: counterboreCycle(program, op) }
}

/** The rapids of a counterbore to each centre at safe Z, as of a drill; the emitter writes the cycle. */
export function planCounterbore(program: QuickProgram, operation: Operation, out: PathBuilder): CounterborePlan {
  const plan = counterborePlan(program, operation)
  for (const hole of plan.points) out.add('rapid', { x: hole.x, y: hole.y, z: program.safeZ })
  return plan
}
