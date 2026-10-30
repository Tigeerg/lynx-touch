// Hole centres, the cycle and its planes (PLAN-QUICKPROG, 7.4 and 10), the port of `path_drill.cpp`.
//
// The hole centres are the whole geometry of drilling: the control expands the cycle, so the
// planner lays out the centres (they come from `drill-holes.ts`) and the emitter writes the cycle.
// The moves of a drilling span are only the rapids of the tool to each hole at safe Z; the
// drilling itself is charged to `cycleMinutes`, the descent and the retraction of the drill at the
// working feed. A peck cycle goes down the same distance: the rapid hops between the pecks are not
// counted, so the estimate is optimistic, as the console already says.
//
// The cycle: an asked peck wins; otherwise a hole deeper than three diameters pecks (G83 with Q of
// one diameter), a shallower one is drilled in one go (G81). The R plane is a value of the document
// at or above the material top (clamped to safe Z), else one millimetre above the top.
import type { DrillOperation, Operation, QuickProgram } from './ops'
import { drillHoles } from './drill-holes'
import { PathBuilder } from './path-builder'

/** The R plane above the material top when the document does not set it, mm. */
const RISE_MM = 1
/** A hole deeper than this many diameters pecks, G83. */
const PECK_TO_DIAMETER = 3

/** The drilling cycle as the emitter writes it: G81 or G83 with its planes. */
export interface DrillCycle {
  /** True for G83 (pecking), false for G81. */
  peck: boolean
  /** The Z of the hole bottom, mm. */
  depthZ: number
  /** The R plane, mm. */
  rZ: number
  /** The Q of G83, mm; zero for G81. */
  peckMm: number
  feedMmMin: number
}

/** The half-open Z interval of a drilling cycle: what the operator form shows and the check reads. */
export interface DrillPlanes {
  /** The top of the material for the operation, mm. */
  topZ: number
  /** The Z of the hole bottom, mm. */
  depthZ: number
  /** The R plane the cycle retracts to, mm. */
  rZ: number
}

/** Everything the emitter and the time estimate read of one drilling operation. */
export interface DrillPlan {
  points: { x: number; y: number }[]
  /** The layout errors in Russian; a layout with errors plans nothing. */
  errors: string[]
  cycle: DrillCycle
  planes: DrillPlanes
}

const drill = (op: Operation): DrillOperation => {
  if (op.kind !== 'drill') throw new Error(`planDrill called with a ${op.kind} operation`)
  return op
}

/** The cycle of a drilling operation: G81 or G83, its planes and the peck. */
export function drillCycle(program: QuickProgram, operation: DrillOperation): DrillCycle {
  const deep = operation.depthMm > PECK_TO_DIAMETER * operation.tool.diameterMm + 1e-9
  const peck = operation.peckMm > 0 || deep
  const peckMm = peck ? (operation.peckMm > 0 ? operation.peckMm : operation.tool.diameterMm) : 0
  const top = operation.topZ
  const defaultR = Math.max(Math.min(top + RISE_MM, program.safeZ), top)
  // The document has no explicit R of a cycle in schema 1: the form may set it later, and the
  // default is the one above.
  const rZ = defaultR
  return { peck, depthZ: top - operation.depthMm, rZ, peckMm, feedMmMin: operation.cutting.feedMmMin }
}

/** The hole centres of the operation, or its layout errors. */
export function drillPlan(program: QuickProgram, operation: Operation): DrillPlan {
  const op = drill(operation)
  const layout = drillHoles(op)
  const cycle = drillCycle(program, op)
  const planes = { topZ: op.topZ, depthZ: cycle.depthZ, rZ: cycle.rZ }
  return { points: layout.points, errors: layout.errors, cycle, planes }
}

/**
 * The rapids of a drilling operation and its cycle time, appended to `out`; the emitter writes the
 * cycle itself from the same layout. A layout with no holes plans nothing.
 */
export function planDrill(program: QuickProgram, operation: Operation, out: PathBuilder): DrillPlan {
  const plan = drillPlan(program, operation)
  if (plan.points.length === 0) return plan
  for (const hole of plan.points) out.add('rapid', { x: hole.x, y: hole.y, z: program.safeZ })
  return plan
}

/** The drilling time of the holes, minutes: the descent and the retraction at the working feed. */
export function drillCycleMinutes(operation: DrillOperation, points: number): number {
  const feed = operation.cutting.feedMmMin
  if (feed <= 0) return 0
  return (points * 2 * operation.depthMm) / feed
}

/** True when the operation is a drilling one; the emitter prints its cycle, not its moves. */
export const isDrillKind = (op: Operation): boolean => op.kind === 'drill'
