// The cycle time of the Q5 kinds (PLAN-QUICKPROG 7.6): the G82 seats and the thread helix.
//
// A canned cycle is a machine feature, not a path: the drill cycle time is charged in `path.ts` on
// top of the moves, and the counterbore and the thread follow the same rule here. A counterbore goes
// down and back at the working feed and dwells `P` seconds at every floor. A thread mill walks the
// helix of every radial pass at its feed, so every pass costs its helix length at the working feed.
// A thread is a path: the emitter prints its arcs and `estimateSeconds` charges them, so the plan
// carries only the counterboring cycle time, and the helix length of a thread is charged as the
// arcs it is made of, so a thread is never charged twice.
import { counterborePlan } from './path-counterbore'
import type { CounterboreOperation, Operation, QuickProgram } from './ops'

const TAU = 2 * Math.PI

/** The counterboring time of the seats, minutes: the descent and the retraction plus the dwells. */
export function counterboreMinutes(program: QuickProgram, operation: Operation): number {
  if (operation.kind !== 'counterbore') return 0
  const cb: CounterboreOperation = operation
  const plan = counterborePlan(program, cb)
  const points = plan.points.length
  const feed = cb.cutting.feedMmMin
  const cut = feed > 0 ? (points * 2 * cb.depthMm) / feed : 0
  return cut + (points * cb.dwellSec) / 60
}
