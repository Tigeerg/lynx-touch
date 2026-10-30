// The copies of an operation: a grid and a turn (PLAN-QUICKPROG, 7.6, «Массив и поворот»).
//
// A copy is not a kind of the document: it is the same operation with its geometry moved, so the
// `.qprog` has no array word and the list holds every copy as its own entry. The reducer flattens
// the copies on `CAM_ARRAY`/`CAM_ROTATE`; this module is the one switch that decides what a kind
// can copy and how its geometry moves, and the switch is exhaustive, so a new kind does not compile
// until it is decided.
//
// The shift is a translation of the geometry only: the frame of the pass (`topZ`, `depth`, the
// cutting data) stays as it is, and the copy keeps the id plus ` #n`, so two copies of one source
// are told apart in the list and in the viewer. A `custom` block is refused: its lines are code,
// and moving code without reading it is a guess. A pocket turns by its corners into the bounding
// rectangle, which is a rectangle only at quarters of a turn.
import type { Operation, Point3, Rect } from './ops'

/** The most copies one operation may be flattened into, the limit of PLAN-QUICKPROG 7.6. */
export const MAX_COPIES = 100

/** The id of a copy: `<source> #2`, `<source> #3`, ...; the source keeps its own id. */
export const copyId = (id: string, n: number): string => {
  const match = /^(.*) #(\d+)$/.exec(id)
  return match === null ? `${id} #${String(n)}` : `${match[1]} #${String(Number(match[2]) + n - 1)}`
}

/** A point moved by `(dx, dy)`; `z` is untouched, the copies stay in the same plane. */
const shifted = (point: Point3, dx: number, dy: number): Point3 => ({ ...point, x: point.x + dx, y: point.y + dy })

/** A point turned around `(cx, cy)` by `angleDeg`, the machine's positive direction. */
const turned = (point: Point3, angleDeg: number, cx: number, cy: number): Point3 => {
  const angle = (angleDeg * Math.PI) / 180
  const cos = Math.cos(angle)
  const sin = Math.sin(angle)
  const dx = point.x - cx
  const dy = point.y - cy
  return { ...point, x: cx + dx * cos - dy * sin, y: cy + dx * sin + dy * cos }
}

/** The four corners of a rectangle turned around a point, and their bounding rectangle. */
const turnedRect = (rect: Rect, angleDeg: number, cx: number, cy: number): Rect => {
  const corners = [
    { x: rect.x, y: rect.y, z: 0 },
    { x: rect.x + rect.w, y: rect.y, z: 0 },
    { x: rect.x + rect.w, y: rect.y + rect.h, z: 0 },
    { x: rect.x, y: rect.y + rect.h, z: 0 },
  ].map((point) => turned(point, angleDeg, cx, cy))
  const xs = corners.map((point) => point.x)
  const ys = corners.map((point) => point.y)
  const x = Math.min(...xs)
  const y = Math.min(...ys)
  return { x, y, w: Math.max(...xs) - x, h: Math.max(...ys) - y }
}

/** True when the angle is a whole quarter of a turn: only then a pocket stays a rectangle. */
export const isQuarterTurn = (angleDeg: number): boolean => {
  const quarters = angleDeg / 90
  return Number.isFinite(quarters) && Math.abs(quarters - Math.round(quarters)) <= 1e-9
}

/** How a turn of a kind is decided, so the reducer can say why a kind is refused. */
type TurnRule = { rule: 'shift' } | { rule: 'points' } | { rule: 'quarter' } | { rule: 'refused'; reason: string }

/** The turn rule of a kind; the switch is exhaustive, a new kind is a compile error. */
function turnRule(op: Operation): TurnRule {
  switch (op.kind) {
    case 'face':
      return { rule: 'refused', reason: 'Плоскость не поворачивается: слой снимается по всей заготовке.' }
    case 'pocket':
      return { rule: 'quarter' }
    case 'circle-pocket':
    case 'drill':
      return { rule: 'shift' }
    case 'contour':
    case 'slot':
    case 'chamfer':
    case 'tap':
      // A tap turns by its hole centres: the pitch is the feed per turn of the spindle, and a turn
      // of the layout around the part origin does not change it.
      return { rule: 'points' }
    case 'custom':
      return { rule: 'refused', reason: 'Custom G Code не копируется: его строки — готовый код, а не геометрия.' }
    case 'probe':
      // A probe point is a measurement of the part, not a cut: moving it documents another touch.
      return { rule: 'points' }
  }
}

/** The reason a kind cannot be copied, or null when it can. */
export const copyRefusal = (op: Operation): string | null => {
  const decided = turnRule(op)
  return decided.rule === 'refused' ? decided.reason : null
}

/**
 * A copy of `op` moved by `(dx, dy)` and, when `angleDeg` is not zero, turned by that angle around
 * `(cx, cy)`. Null when the kind is not copied, or when a pocket would turn by something other than
 * a quarter of a turn and stop being a rectangle. The input is not mutated.
 */
export function cloneOperation(
  op: Operation,
  dx: number,
  dy: number,
  angleDeg: number,
  cx: number,
  cy: number,
  n = 2,
): Operation | null {
  const decided = turnRule(op)
  if (decided.rule === 'refused') return null
  const id = copyId(op.id, n)
  // A translation only is the cheap path; a turn composes with it, the shift first, then the turn.
  const place = (point: Point3): Point3 => turned(shifted(point, dx, dy), angleDeg, cx, cy)
  switch (op.kind) {
    case 'face':
    case 'custom':
      // Refused before the switch: the turn rule of both is 'refused'.
      return null
    case 'pocket': {
      if (angleDeg !== 0 && !isQuarterTurn(angleDeg)) return null
      const shiftedRect = { ...op.rect, x: op.rect.x + dx, y: op.rect.y + dy }
      return { ...op, id, rect: angleDeg === 0 ? shiftedRect : turnedRect(shiftedRect, angleDeg, cx, cy) }
    }
    case 'circle-pocket':
      return { ...op, id, circle: { ...op.circle, ...place({ ...op.circle, z: 0 }) } }
    case 'drill':
      return { ...op, id, hole: { ...op.hole, ...place({ ...op.hole, z: 0 }) }, ...placedLayout(op, place) }
    case 'tap':
      return { ...op, id, hole: { ...op.hole, ...place({ ...op.hole, z: 0 }) }, ...placedLayout(op, place) }
    case 'contour':
      return { ...op, id, points: op.points.map(place) }
    case 'slot':
    case 'chamfer':
      return { ...op, id, from: place(op.from), to: place(op.to) }
    case 'probe':
      return { ...op, id, points: op.points.map(place) }
  }
}

/** The grid origin of a drilling or tapping layout, moved; the steps and the counts stay. */ const placedLayout = (
  op: Extract<Operation, { kind: 'drill' | 'tap' }>,
  place: (point: Point3) => Point3,
): { x0: number; y0: number } => {
  const origin = place({ x: op.x0, y: op.y0, z: 0 })
  return { x0: origin.x, y0: origin.y }
}

/** The source with its copies right after it, or the reason the copies cannot be made. */
export type CopyResult = { ok: true; operations: Operation[] } | { ok: false; reason: string }

/** True for a whole positive count of copies; a grid of 1x1 makes none and is refused as empty. */
export const isCount = (value: number): boolean => Number.isInteger(value) && value > 0

/**
 * The copies of one operation as a flat list, the source first and its copies right after it. The
 * grid is `cols x rows`, the source is the cell (0, 0) and a cell (i, j) is shifted by
 * `(i * dx, j * dy)`; the turn is `copies` angles of `angleDeg` around `(cx, cy)`. A total of
 * more than `MAX_COPIES` operations (the source included) is refused whole: the document keeps its
 * list.
 */
export function arrayCopies(op: Operation, grid: { cols: number; rows: number; dx: number; dy: number }): CopyResult {
  if (!isCount(grid.cols) || !isCount(grid.rows))
    return { ok: false, reason: 'Число столбцов и рядов должно быть целым и больше нуля.' }
  const total = grid.cols * grid.rows
  if (total > MAX_COPIES) return { ok: false, reason: `Слишком много копий: больше ${String(MAX_COPIES)}` }
  if (total === 1) return { ok: false, reason: 'Сетка 1 на 1 не даёт копий: задайте больше столбцов или рядов.' }
  const refused = copyRefusal(op)
  if (refused !== null) return { ok: false, reason: refused }
  const copies: Operation[] = [op]
  let n = 2
  for (let j = 0; j < grid.rows; j += 1) {
    for (let i = 0; i < grid.cols; i += 1) {
      if (i === 0 && j === 0) continue
      const copy = cloneOperation(op, i * grid.dx, j * grid.dy, 0, 0, 0, n)
      if (copy === null) return { ok: false, reason: copyRefusal(op) ?? 'Операция не копируется.' }
      copies.push(copy)
      n += 1
    }
  }
  return { ok: true, operations: copies }
}

/** The turned copies of one operation, the source first, or the reason they cannot be made. */
export function rotatedCopies(
  op: Operation,
  turn: { angleDeg: number; cx: number; cy: number; copies: number },
): CopyResult {
  if (!isCount(turn.copies)) return { ok: false, reason: 'Число копий должно быть целым и больше нуля.' }
  if (turn.copies + 1 > MAX_COPIES) return { ok: false, reason: `Слишком много копий: больше ${String(MAX_COPIES)}` }
  const refused = copyRefusal(op)
  if (refused !== null) return { ok: false, reason: refused }
  if (op.kind === 'pocket' && !isQuarterTurn(turn.angleDeg)) {
    return { ok: false, reason: 'Поворот кармана — только кратно 90 градусам.' }
  }
  const copies: Operation[] = [op]
  for (let k = 1; k <= turn.copies; k += 1) {
    const copy = cloneOperation(op, 0, 0, turn.angleDeg * k, turn.cx, turn.cy, k + 1)
    if (copy === null) return { ok: false, reason: copyRefusal(op) ?? 'Операция не поворачивается.' }
    copies.push(copy)
  }
  return { ok: true, operations: copies }
}

/**
 * A copy of `op` turned by `angleDeg * n` around `(cx, cy)`: the copy number `#n+1`. Null on the
 * same refusals as `cloneOperation` — a kind that is not copied, or a pocket on an angle that is
 * not a quarter of a turn.
 */
export function rotateOperation(op: Operation, angleDeg: number, cx: number, cy: number, n: number): Operation | null {
  return cloneOperation(op, 0, 0, angleDeg * n, cx, cy, n + 1)
}
