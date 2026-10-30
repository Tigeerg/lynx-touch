// The offset of a contour to the mill centre (PLAN-QUICKPROG, 7.6): one geometry for the planner
// and the corner rule.
//
// Every segment is shifted by `D / 2 + allowance` along its normal. The material is left of the walk
// (a counter-clockwise closed profile), so a material offset moves the centre to that side, the
// `air` side the other way. Two offset lines meet on the bisector of the vertex, `inset / sin(a/2)`
// from it for its opening angle `a`; the corner rule of `validate-contour.ts` reads the same angle.
//
// A turn smaller than `COLLINEAR_DEG` continues the line: the intersection of two nearly parallel
// lines runs away, so the offset point of the second segment stands instead. `segments` walks a
// closed contour back to its first vertex; an open one stops at the last segment.
import type { Point3 } from './ops'

/** Below this turn two segments are one line, degrees: the joint is a plain continuation. */
export const COLLINEAR_DEG = 1

/** A point of XY in millimetres; the contour geometry is flat and ignores the `z` of a vertex. */
export interface Vec2 {
  x: number
  y: number
}

/** The plane point of a contour vertex: the `z` of the document is not the plane of the cut. */
export const flat = (point: Point3): Vec2 => ({ x: point.x, y: point.y })

const sub = (a: Vec2, b: Vec2): Vec2 => ({ x: a.x - b.x, y: a.y - b.y })
export const add = (a: Vec2, b: Vec2): Vec2 => ({ x: a.x + b.x, y: a.y + b.y })
export const mul = (a: Vec2, b: number): Vec2 => ({ x: a.x * b, y: a.y * b })
const dot = (a: Vec2, b: Vec2): number => a.x * b.x + a.y * b.y
const cross = (a: Vec2, b: Vec2): number => a.x * b.y - a.y * b.x
const length = (a: Vec2): number => Math.hypot(a.x, a.y)

/** The unit direction of a segment, or `null` for two equal points. */
export const direction = (from: Vec2, to: Vec2): Vec2 | null => {
  const d = sub(to, from)
  const len = length(d)
  return len < 1e-12 ? null : mul(d, 1 / len)
}

/** The left normal of a unit direction: the side the material stands on of a CCW walk. */
export const leftNormal = (dir: Vec2): Vec2 => ({ x: -dir.y, y: dir.x })

/** The signed side of the offset: `+1` along the left normal (toward the material), `-1` outside. */
export const sideSign = (side: 'material' | 'air'): number => (side === 'material' ? 1 : -1)

/** The offset vector of a segment: the centre moves `inset` toward the material or outside. */
export const offsetVector = (dir: Vec2, inset: number, side: 'material' | 'air'): Vec2 => {
  const left = leftNormal(dir)
  const sign = sideSign(side)
  return { x: left.x * inset * sign, y: left.y * inset * sign }
}

/**
 * The intersection of two lines, each by a point and a unit direction, or `null` for parallel ones.
 * `p + s * u = q + t * v`; the cross product of the directions is the denominator.
 */
export function intersect(p: Vec2, u: Vec2, q: Vec2, v: Vec2): Vec2 | null {
  const den = cross(u, v)
  if (Math.abs(den) < 1e-12) return null
  const t = cross(sub(q, p), u) / den
  return add(q, mul(v, t))
}

/** The angle between a segment and the one after it, radians, 0 for a straight continuation. */
export const turnRad = (before: Vec2, after: Vec2): number => {
  const c = Math.min(1, Math.max(-1, dot(before, after)))
  return Math.acos(c)
}

export interface Joint {
  /** The offset point of the vertex. */
  centre: Vec2
  /** The half angle between the walls, radians: the bisector is `inset / sin(half)` long. */
  halfRad: number
  /** The opening angle of the corner (`half` is its half), radians: pi minus the turn of the walk. */
  openingRad: number
  /** True when the segments are collinear and the joint is a plain continuation. */
  collinear: boolean
}

/**
 * The offset point of an inner vertex and the angles of its corner. `before`/`after` are the unit
 * directions into and out of the vertex; a collinear pair continues the first line.
 *
 * The opening angle of the material is a half turn minus the turn of the walk; the joint lies on its
 * bisector at `inset / sin(opening / 2)` from the vertex.
 */
export function joint(vertex: Vec2, before: Vec2, after: Vec2, inset: number, side: 'material' | 'air'): Joint {
  const turn = turnRad(before, after)
  // A half turn (180 degrees) as `2 * pi / 2`, so the corner reads by the same arithmetic as the
  // quarter turn of the corner rule in `validate-contour.ts`.
  const openingRad = (2 * Math.PI) / 2 - turn
  const halfRad = (2 * Math.PI) / 4 - turn / 2
  const onBefore = add(vertex, offsetVector(before, inset, side))
  const onAfter = add(vertex, offsetVector(after, inset, side))
  if (turn < (2 * Math.PI * COLLINEAR_DEG) / 360) {
    return { centre: onAfter, halfRad, openingRad: (2 * Math.PI) / 2, collinear: true }
  }
  return { centre: intersect(onBefore, before, onAfter, after) ?? onAfter, halfRad, openingRad, collinear: false }
}

/** The segments of the walk: `closed` walks the last vertex back to the first. */
export function segments(points: Point3[], closed: boolean): { from: Vec2; to: Vec2; dir: Vec2 }[] {
  const flatPoints = points.map(flat)
  const pairs: [Vec2, Vec2][] = []
  for (let i = 0; i + 1 < flatPoints.length; i += 1) pairs.push([flatPoints[i], flatPoints[i + 1]])
  if (closed && flatPoints.length > 1) pairs.push([flatPoints[flatPoints.length - 1], flatPoints[0]])
  const out: { from: Vec2; to: Vec2; dir: Vec2 }[] = []
  for (const [from, to] of pairs) {
    const dir = direction(from, to)
    if (dir !== null) out.push({ from, to, dir })
  }
  return out
}
