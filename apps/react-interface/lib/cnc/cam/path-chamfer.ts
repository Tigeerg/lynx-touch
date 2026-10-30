// The chamfer planner (PLAN-QUICKPROG, 7.6): take a straight edge off with the corner of the mill.
//
// The mill body stands over the material, its corner on the edge: the tool centre runs `from` -> `to`
// over the edge and is sunk `chamferMm` below the top, so the corner leaves a triangle of the edge of
// that leg. The entry and the exit go off the edge along its normal by `D / 3`, so the mill is not
// dropped onto the wall and the cut is not opened at a full depth. The cut is split by the axial step
// from the top, and the last pass is exactly `topZ - chamferMm`; a chamfer is usually one pass.
//
// Only a straight edge is modelled: an arc of the edge is not a chamfer here (PLAN-QUICKPROG, 7.6,
// "arcs of a chamfer are a warning" - a slot of one segment has no arc, so there is nothing to warn
// about). `grain` is kept on the operation for a later planner and is not read by the cut.
import type { ChamferOperation, Operation, ProgramShape } from './ops'
import { direction } from './contour-offset'
import type { Vec2 } from './contour-offset'
import { axialPasses } from './path-face'
import { PathBuilder } from './path-builder'

/** The lead of a chamfer along the normal, share of the diameter. */
export const CHAMFER_LEAD_SHARE = 1 / 3

/** The chamfer layout: the edge, the arm points and the axial passes. */
export interface ChamferLayout {
  /** The mill centre at the start and at the end of the edge. */
  entry: Vec2
  exit: Vec2
  /** The point the mill plunges at, `lead` off the edge along the normal before the entry. */
  arm: Vec2
  /** The point the mill leaves from, `lead` off the edge on the same side past the exit. */
  leave: Vec2
  /** The lead along the normal off the edge at the entry and at the exit, mm. */
  leadMm: number
  /** The Z of each pass, top down; the last is exactly the chamfer bottom. */
  passes: number[]
}

const chamfer = (op: Operation): ChamferOperation => {
  if (op.kind !== 'chamfer') throw new Error(`planChamfer called with a ${op.kind} operation`)
  return op
}

/**
 * The layout of a chamfer. `axialPasses` reads the depth it is given, so the layout passes the
 * chamfer depth, not the operation's: the bottom of the cut is `topZ - chamferMm`.
 */
export function chamferLayout(operation: ChamferOperation): ChamferLayout {
  const op = chamfer(operation)
  const entry = { x: op.from.x, y: op.from.y }
  const exit = { x: op.to.x, y: op.to.y }
  const passes = axialPasses(op.topZ, op.chamferMm, op.stepDownMm)
  const leadMm = op.tool.diameterMm * CHAMFER_LEAD_SHARE
  const dir = direction(entry, exit)
  // The edge is a point, not a line: the arm falls back on the entry, and the planner cuts nothing.
  if (dir === null) return { entry, exit, arm: entry, leave: exit, leadMm, passes }
  // The lead is along the normal off the edge: the mill comes down `D/3` to one side of the wall and
  // keeps `D/3` past the end on that same side, so it never drops onto the wall. The normal's sign is
  // arbitrary (the chamfer is symmetric); the left normal of the walk is taken.
  const normal = { x: -dir.y, y: dir.x }
  const arm = { x: entry.x - normal.x * leadMm, y: entry.y - normal.y * leadMm }
  const leave = { x: exit.x - normal.x * leadMm, y: exit.y - normal.y * leadMm }
  return { entry, exit, arm, leave, leadMm, passes }
}

/** The chamfer of one operation, appended to `out`. A valid document is assumed. */
export function planChamfer(
  program: ProgramShape & { safeZ: number },
  operation: Operation,
  out: PathBuilder,
): ChamferLayout {
  const op = chamfer(operation)
  const layout = chamferLayout(op)
  if (layout.passes.length === 0) return layout
  const safeZ = program.safeZ
  const top = op.topZ
  const at = (point: Vec2, z: number) => ({ x: point.x, y: point.y, z })
  for (const planeZ of layout.passes) {
    out.add('rapid', at(layout.arm, safeZ))
    out.add('rapid', at(layout.arm, top))
    out.add('cut', at(layout.arm, planeZ), op.cutting.plungeFeedMmMin)
    out.add('cut', at(layout.entry, planeZ), op.cutting.feedMmMin)
    out.add('cut', at(layout.exit, planeZ), op.cutting.feedMmMin)
    out.add('cut', at(layout.leave, planeZ), op.cutting.feedMmMin)
    out.add('cut', at(layout.leave, top), op.cutting.plungeFeedMmMin)
  }
  return layout
}
