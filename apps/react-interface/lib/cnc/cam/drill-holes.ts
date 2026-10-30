// The hole centres of a drilling operation, in cutting order (PLAN-QUICKPROG, 7.2).
//
// The centres are the whole geometry of drilling: the control expands the cycle, so the planner
// writes the centres and the check tests each one against the stock. A grid goes row by row along
// X; a ring runs clockwise from `startDeg`, with `fullTurn` spreading `count` holes on the closed
// circle and an arc spreading them over `arcDeg` including the last point.
import type { DrillOperation } from './ops'
import { fmt } from './feeds'

/** The XY centres of the holes and the layout errors in Russian; an error gives no points. */
export interface HoleLayout {
  points: { x: number; y: number }[]
  errors: string[]
}

const AXIS_EPS = 1e-12

/** A cosine or sine of a multiple of pi that belongs on an axis is put on it exactly. */
const onAxis = (value: number): number => (Math.abs(value) < AXIS_EPS ? 0 : value)

/** The hole centres of the layout, or the reasons the layout cannot be laid out. */
export function drillHoles(op: DrillOperation): HoleLayout {
  if (op.pattern === 'grid') return gridHoles(op)
  if (op.pattern === 'circle') return ringHoles(op)
  return { points: [{ x: op.hole.x, y: op.hole.y }], errors: [] }
}

function gridHoles(op: DrillOperation): HoleLayout {
  if (op.cols <= 0 || op.rows <= 0) {
    return {
      points: [],
      errors: [`Сетка отверстий: число по X и по Y должно быть больше нуля, а задано ${op.cols} и ${op.rows}.`],
    }
  }
  const points: { x: number; y: number }[] = []
  for (let row = 0; row < op.rows; row += 1) {
    for (let col = 0; col < op.cols; col += 1) points.push({ x: op.x0 + op.dx * col, y: op.y0 + op.dy * row })
  }
  return { points, errors: [] }
}

function ringHoles(op: DrillOperation): HoleLayout {
  if (op.count <= 0) {
    return { points: [], errors: [`Круг отверстий: число отверстий должно быть больше нуля, а задано ${op.count}.`] }
  }
  if (op.hole.r <= 0) {
    return { points: [], errors: [`Круг отверстий: радиус окружности ${fmt(op.hole.r)} мм должен быть больше нуля.`] }
  }
  const start = (op.startDeg * Math.PI) / 180
  const step = op.fullTurn
    ? (2 * Math.PI) / op.count
    : (op.arcDeg * Math.PI) / 180 / (op.count - 1 > 0 ? op.count - 1 : 1)
  const points: { x: number; y: number }[] = []
  for (let i = 0; i < op.count; i += 1) {
    // Clockwise: the angle decreases, as the console lays the ring out.
    const angle = start - step * i
    points.push({
      x: op.hole.x + op.hole.r * onAxis(Math.cos(angle)),
      y: op.hole.y + op.hole.r * onAxis(Math.sin(angle)),
    })
  }
  return { points, errors: [] }
}
