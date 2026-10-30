// The entities of the DXF `ENTITIES` section (PLAN-QUICKPROG, 7.6, «Импорт DXF»).
//
// The records of the section, their pairs and the read of one entity: a LINE is an open two-point
// contour, an LWPOLYLINE takes its bulges (`bulgeArm`), an ARC and a CIRCLE become chords, and the
// old-style POLYLINE reads its VERTEX records up to the SEQEND. A type outside the list is `null`:
// the caller counts it in `skipped` instead of refusing the file.
import type { DxfContour, DxfPoint, Pair } from './dxf-read'
import { CHORD_TOL_MM } from './dxf-read'

/** Two vertices closer than this are one point, mm: the ends of a LINE and the next LINE meet. */
export const JOIN_EPS_MM = 1e-6

/** The points of one LWPOLYLINE: the vertices and the bulges in the order of the file. */
function lwPolyline(body: Pair[], chordTol: number): { points: DxfPoint[]; closed: boolean } {
  const closed = ((numbers(body, 70)[0] ?? 0) & 1) === 1
  const vertices: { point: DxfPoint; bulge: number }[] = []
  let current: { point: DxfPoint; bulge: number } | null = null
  for (const pair of body) {
    if (pair.code === 10) {
      current = { point: { x: Number(pair.value), y: NaN }, bulge: 0 }
      vertices.push(current)
    } else if (pair.code === 20 && current !== null) {
      current.point.y = Number(pair.value)
    } else if (pair.code === 42 && current !== null) {
      current.bulge = Number(pair.value)
    }
  }
  // The arms are taken pairwise: each vertex leads to the next one, and a closed polyline leads
  // its last vertex back to the first.
  const good = vertices.filter((item) => finite(item.point))
  const points: DxfPoint[] = []
  const arms = closed ? good.length : good.length - 1
  for (let i = 0; i < arms; i += 1) {
    const from = good[i]
    const to = good[(i + 1) % good.length]
    points.push(...bulgeArm(from.point, to.point, from.bulge, chordTol))
  }
  if (!closed && good.length > 0) points.push(good[good.length - 1].point)
  return { points, closed: closed && good.length > 2 }
}

/** One entity as contours (a LINE is one open two-point contour), `null` for an unsupported type. */
export function entityContours(type: string, body: Pair[], chordTol = CHORD_TOL_MM): DxfContour[] | null {
  switch (type) {
    case 'LINE': {
      const [from, to] = [pointOf(body, 10, 20), pointOf(body, 11, 21)]
      return finite(from) && finite(to) ? [{ points: [from, to], closed: false }] : []
    }
    case 'LWPOLYLINE': {
      const { points, closed } = lwPolyline(body, chordTol)
      return points.length >= 2 ? [{ points, closed }] : []
    }
    case 'POLYLINE':
      // Handled with the VERTEX/SEQEND records by `read`.
      return []
    case 'SEQEND':
      return []
    case 'VERTEX':
      return []
    case 'ARC': {
      const centre = pointOf(body, 10, 20)
      const r = number(body, 40)
      const start = (number(body, 50) * Math.PI) / 180
      const end = (number(body, 51) * Math.PI) / 180
      const sweep = (end - start + 2 * Math.PI) % (2 * Math.PI) || 2 * Math.PI
      return finite(centre) && r > 0
        ? [{ points: arcPoints(centre.x, centre.y, r, start, sweep, chordTol), closed: false }]
        : []
    }
    case 'CIRCLE': {
      const centre = pointOf(body, 10, 20)
      const r = number(body, 40)
      if (!finite(centre) || !(r > 0)) return []
      const points = arcPoints(centre.x, centre.y, r, 0, 2 * Math.PI, chordTol).slice(0, -1)
      return [{ points, closed: true }]
    }
    default:
      return null
  }
}

/** The contours of the old-style POLYLINE: its VERTEX records up to the SEQEND. */
export function polylineOf(entities: DxfEntity[], head: DxfEntity, at: number): DxfContour[] {
  const closed = ((numbers(head.pairs, 70)[0] ?? 0) & 1) === 1
  const points: DxfPoint[] = []
  for (let i = at + 1; i < entities.length && entities[i].type !== 'SEQEND'; i += 1) {
    if (entities[i].type !== 'VERTEX') continue
    const point = pointOf(entities[i].pairs, 10, 20)
    if (finite(point)) points.push(point)
  }
  return points.length >= 2 ? [{ points, closed: closed && points.length > 2 }] : []
}

/** One entity of the section: the type word and its pairs. */
export interface DxfEntity {
  type: string
  pairs: Pair[]
}

/** The entity records of the `ENTITIES` section: the type and its pairs, or `null` without it. */
export function entitiesOf(pairs: Pair[]): DxfEntity[] | null {
  let inside = false
  let found = false
  const entities: DxfEntity[] = []
  for (let i = 0; i < pairs.length; i += 1) {
    const pair = pairs[i]
    if (pair.code !== 0) continue
    if (pair.value === 'SECTION') {
      // The section name is the pair of code 2 right after the word; a walk over it starts there.
      inside = pairs[i + 1]?.code === 2 && pairs[i + 1]?.value === 'ENTITIES'
      found ||= inside
      if (inside) i += 1
      continue
    }
    if (!inside) continue
    if (pair.value === 'ENDSEC') {
      inside = false
      continue
    }
    // The pairs of the entity follow its `0` word: the loop starts from the next one.
    const body: Pair[] = []
    while (i + 1 < pairs.length && pairs[i + 1].code !== 0) {
      i += 1
      body.push(pairs[i])
    }
    entities.push({ type: pair.value, pairs: body })
  }
  return found ? entities : null
}

/** The numbers of one group code in the order of the file. */
export const numbers = (body: Pair[], code: number): number[] =>
  body.filter((pair) => pair.code === code).map((pair) => Number(pair.value))

/** The first number of the code, or `NaN` when the entity has none. */
export const number = (body: Pair[], code: number): number => numbers(body, code)[0] ?? NaN

/** A point from the codes of one group: the point, or both coordinates `NaN` without the pair. */
export const pointOf = (body: Pair[], x: number, y: number): DxfPoint => {
  const px = number(body, x)
  return { x: px, y: Number.isFinite(px) ? number(body, y) : NaN }
}

/** True when both coordinates are finite numbers. */
export const finite = (point: DxfPoint): boolean => Number.isFinite(point.x) && Number.isFinite(point.y)

/** The same point within the join tolerance. */
export const same = (a: DxfPoint, b: DxfPoint): boolean =>
  Math.abs(a.x - b.x) <= JOIN_EPS_MM && Math.abs(a.y - b.y) <= JOIN_EPS_MM

/** The chord count of an arc: `ceil(theta / phi)` with the half angle `phi` from the sagitta. */
export function chordCount(radius: number, sweepRad: number, chordTol = CHORD_TOL_MM): number {
  const r = Math.abs(radius)
  if (r <= chordTol) return 1
  const half = Math.acos(1 - chordTol / r)
  return Math.max(1, Math.ceil(sweepRad / (2 * half)))
}

/** The points of an arc: the centre, the radius, the start and the sweep (radians, signed). */
export function arcPoints(
  cx: number,
  cy: number,
  r: number,
  startRad: number,
  sweepRad: number,
  chordTol = CHORD_TOL_MM,
): DxfPoint[] {
  const count = chordCount(r, Math.abs(sweepRad), chordTol)
  const step = sweepRad / count
  const points: DxfPoint[] = []
  for (let i = 0; i <= count; i += 1) {
    const angle = startRad + step * i
    points.push({ x: cx + r * Math.cos(angle), y: cy + r * Math.sin(angle) })
  }
  return points
}

/**
 * The points of one LWPOLYLINE arm: the vertex and the bulge that leads out of it, `bulge` is
 * `tan(theta / 4)` and a positive one turns counter-clockwise. The last point of the arm is the next
 * vertex, so the arms do not repeat it.
 */
export function bulgeArm(from: DxfPoint, to: DxfPoint, bulge: number, chordTol = CHORD_TOL_MM): DxfPoint[] {
  if (bulge === 0 || !Number.isFinite(bulge)) return [from]
  const dx = to.x - from.x
  const dy = to.y - from.y
  const chord = Math.hypot(dx, dy)
  if (chord === 0) return [from]
  const sweep = 4 * Math.atan(bulge)
  const r = chord / (2 * Math.abs(Math.sin(sweep / 2)))
  // The centre stands on the perpendicular of the chord, `h` from its middle; the sign follows the
  // bulge: a half turn is the middle itself, a bigger r a taller triangle.
  const mx = (from.x + to.x) / 2
  const my = (from.y + to.y) / 2
  const h = chord / 2 / Math.tan(sweep / 2)
  // AutoCAD puts the centre on the left of the chord for a positive bulge (the arc bulges to the
  // right, clockwise in the machine sense); the left normal of the chord is (-dy, dx).
  const cx = mx + (dy / chord) * h
  const cy = my - (dx / chord) * h
  const start = Math.atan2(from.y - cy, from.x - cx)
  const points = arcPoints(cx, cy, r, start, sweep, chordTol)
  // The trig ends differ from the vertices by rounding: the arm keeps the exact vertices.
  points[0] = from
  points[points.length - 1] = to
  return points.slice(0, -1)
}
