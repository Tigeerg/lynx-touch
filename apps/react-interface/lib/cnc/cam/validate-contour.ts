// The contour rules (PLAN-QUICKPROG, 7.6): the profile the mill can cut at all.
//
// A list of at least two points is a contour; fewer is a mistake of the form. The allowance is the
// stock left on the wall, so a negative one moves the path past the profile. The mill body walks
// `D / 2 + allowance` off every wall; at a corner the body touches both walls, and the vertex is
// cut only when the material there fits inside the body. The material of a corner fills its opening
// angle, the body covers a half plane: a corner narrower than half a turn leaves stock the body
// cannot reach, and the centre then stands inside the wall it cannot cut. That corner is refused by
// the number of the vertex.
//
// A contour wholly outside the stock cannot cut the part, and it is refused; a contour that is only
// partly outside is left to the collision check, which already names the move, so the rules here do
// not repeat it. The Z rules (depth, step, safe Z) are common and run in `validate-common.ts`.
import type { ContourOperation, ProgramShape } from './ops'
import { fmt } from './feeds'
import type { Vec2 } from './contour-offset'
import { segments } from './contour-offset'
import { EPS, add } from './validate-common'
import type { Problem } from './validate-common'

/** Below this turn two segments are one line: a vertex of a straight wall, not a corner. */
const STRAIGHT_RAD = 0.017453292519943295

/** The quarter turn of the mill body: the corner opening below it is too sharp for the mill. */
const QUARTER_RAD = 1.5707963267948966

/** The turn from one unit direction to the next, radians: zero for a straight continuation. */
const angle = (from: Vec2, to: Vec2): number => Math.acos(Math.min(1, Math.max(-1, from.x * to.x + from.y * to.y)))

/** A contour needs two points to be a segment at all. */
export function checkContour(program: ProgramShape, op: ContourOperation, out: Problem[]): void {
  const points = op.points
  if (points.length < 2) {
    add(out, 'points', `Контур из ${points.length === 1 ? 'одной точки' : 'нуля точек'}: задайте хотя бы две.`)
    return
  }
  if (op.allowanceMm < 0) {
    add(out, 'points', `Припуск ${fmt(op.allowanceMm)} мм отрицательный: траектория выйдет за контур.`)
    return
  }
  if (whollyOutside(program, points)) {
    const { min, max } = program.stock
    add(
      out,
      'points',
      `Контур целиком вне заготовки: ни одна точка не попадает внутрь X ${fmt(min.x)}..${fmt(max.x)}, ` +
        `Y ${fmt(min.y)}..${fmt(max.y)} мм.`,
    )
    return
  }
  checkCorners(op, out)
}

/** True when no point of the profile stands inside the stock rectangle in XY. */
const whollyOutside = (program: ProgramShape, points: ContourOperation['points']): boolean => {
  const { min, max } = program.stock
  return points.every((p) => p.x < min.x - EPS || p.x > max.x + EPS || p.y < min.y - EPS || p.y > max.y + EPS)
}

/**
 * The corner rule at every vertex: the material opening of the corner is a half turn minus the turn
 * of the walk, and the mill body covers a quarter turn on either side of its centre, so an opening
 * below 90 degrees is a corner the body cannot enter. The ends of an open contour carry the lead,
 * not a corner, so they are not tested.
 */
function checkCorners(op: ContourOperation, out: Problem[]): void {
  const radius = op.tool.diameterMm / 2
  const path = segments(op.points, op.closed)
  if (path.length === 0) return
  const indices = op.closed ? op.points.map((_, i) => i) : op.points.map((_, i) => i).slice(1, -1)
  for (const index of indices) {
    if (index >= path.length) continue
    const into = path[(index - 1 + path.length) % path.length].dir
    const outOf = path[index].dir
    const turn = angle(into, outOf)
    if (turn < STRAIGHT_RAD) continue
    if (angle(outOf, into) < QUARTER_RAD) {
      const reason = `Угол у точки ${index + 1} острее фрезы: радиус фрезы ${fmt(radius)} мм не даёт пройти.`
      out.push({ field: 'points', reason })
    }
  }
}
