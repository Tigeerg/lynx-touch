// The contour planner (PLAN-QUICKPROG, 7.6): the mill centre along the offset profile.
//
// The plan of one Z pass: rapid at safe Z over the entry, rapid down to the material top, plunge to
// the pass Z, the offset polyline at the working feed, and a way back up to the top. A closed
// contour is walked from its first offset point through the joints and back to it; an open one
// leads in and out along the normal by `LEAD_SHARE` of the diameter, so the mill enters the cut off
// the profile and is not dropped onto the wall.
//
// The offset itself is `contour-offset.ts`; this module only orders the moves and the passes. The
// plane of a pass is the plane of the whole polyline: a contour of points has no arcs, so every cut
// is a G1 on the emitter's side.
import type { ContourOperation, Operation, ProgramShape } from './ops'
import type { Vec2 } from './contour-offset'
import { joint, offsetVector, segments } from './contour-offset'
import { axialPasses } from './path-face'
import { PathBuilder } from './path-builder'

/** The lead of an open contour along the normal, share of the diameter. */
export const LEAD_SHARE = 0.3

/** The offset profile: the joint of every vertex, the passes, and the lead of an open contour. */
export interface ContourLayout {
  /** The mill centre at every vertex of the profile, in the order of the walk. */
  corners: Vec2[]
  /** The Z of each pass, top down; the last is exactly the floor. */
  passes: number[]
  /** The lead in and out of an open contour, mm; zero for a closed one. */
  leadMm: number
  /** The offset point of the first vertex of an open contour: the cut starts here after the lead. */
  entry?: Vec2
  /** The offset point of the last vertex of an open contour: the cut ends here, then leaves. */
  exit?: Vec2
}

const contour = (op: Operation): ContourOperation => {
  if (op.kind !== 'contour') throw new Error(`planContour called with a ${op.kind} operation`)
  return op
}

/** The joint of an inner vertex by its index in the walk, from the segments before and after it. */
const vertexJoint = (op: ContourOperation, at: number, path: ReturnType<typeof segments>) => {
  const count = path.length
  // The corner at `at` joins the segment into the vertex and the one out of it; an open contour has
  // `count = n - 1` segments, so the last vertex carries no joint and keeps its offset end.
  const before = path[(at - 1 + count) % count]
  const after = path[at]
  // `joint` offsets the vertex itself, so the vertex of the walk comes in, not its offset point.
  return joint(path[at].from, before.dir, after.dir, op.tool.diameterMm / 2 + op.allowanceMm, op.side)
}

/** The offset profile of a contour: one joint per vertex, and the lead of an open one. */
export function contourLayout(operation: ContourOperation): ContourLayout {
  const op = contour(operation)
  const path = segments(op.points, op.closed)
  const passes = axialPasses(op.topZ, op.depthMm, op.stepDownMm)
  if (path.length === 0) return { corners: [], passes, leadMm: 0 }
  if (op.closed) {
    const corners = op.points.map((_, at) => vertexJoint(op, at, path).centre)
    return { corners, passes, leadMm: 0 }
  }
  // The inner vertices are indexed as in the walk: vertex 1 is the end of segment 0.
  const inner = path.slice(1, -1).map((_, at) => vertexJoint(op, at + 1, path).centre)
  const { leadMm, start, stop, enter, leave } = openEnds(op, path)
  const corners = [enter, ...inner, leave]
  return { corners, passes, leadMm, entry: start, exit: stop }
}

/**
 * The ends of an open contour: the offset point of the first and of the last vertex, the unit offset
 * normal of each end, and the lead. The entry point is `lead` back along the start normal, off the
 * cut; the exit point is `lead` further along the end normal, past the wall the walk leaves.
 */
const openEnds = (op: ContourOperation, path: ReturnType<typeof segments>) => {
  const leadMm = op.tool.diameterMm * LEAD_SHARE
  const normalAt = (end: { dir: { x: number; y: number } }) => {
    const offset = offsetVector(end.dir, 1, op.side)
    return { x: offset.x, y: offset.y }
  }
  const first = path[0]
  const last = path[path.length - 1]
  const start = { x: first.from.x, y: first.from.y }
  const stop = { x: last.to.x, y: last.to.y }
  const into = normalAt(first)
  const out = normalAt(last)
  // The entry is off the cut, the mill goes down away from the wall; the exit passes the wall.
  const enter = { x: start.x - into.x * leadMm, y: start.y + into.y * leadMm }
  const leave = { x: stop.x + out.x * leadMm, y: stop.y + out.y * leadMm }
  return { leadMm, start, stop, enter, leave }
}

/**
 * The contour of one operation, appended to `out`. A valid document is assumed: the corner rule
 * has already refused the angles the mill cannot cut.
 */
export function planContour(
  program: ProgramShape & { safeZ: number },
  operation: Operation,
  out: PathBuilder,
): ContourLayout {
  const op = contour(operation)
  const layout = contourLayout(op)
  if (layout.corners.length === 0) return layout
  const first = layout.corners[0]
  const at = (point: Vec2, z: number) => ({ x: point.x, y: point.y, z })
  for (const planeZ of layout.passes) {
    // The plunge goes down at the lead point, off the cut; the rest of the pass walks the profile.
    out.add('rapid', at(first, program.safeZ))
    out.add('rapid', at(first, op.topZ))
    out.add('cut', at(first, planeZ), op.cutting.plungeFeedMmMin)
    const rest = layout.corners.slice(1)
    for (const corner of rest) out.add('cut', at(corner, planeZ), op.cutting.feedMmMin)
    // The closed walk returns to the entry before the retract; the open one stops at its exit.
    const stop = op.closed ? first : (layout.corners[layout.corners.length - 1] ?? first)
    if (op.closed) out.add('cut', at(stop, planeZ), op.cutting.feedMmMin)
    out.add('cut', at(stop, op.topZ), op.cutting.plungeFeedMmMin)
  }
  return layout
}
