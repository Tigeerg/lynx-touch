// The move collector and the cutting length of the planner (PLAN-QUICKPROG, 7.4, C2).
//
// The planner returns moves in machine coordinates, never G-code: the emitter prints them and the
// preview draws the same list, so the geometry has one source and two representations. The bounds,
// the cut length and the time estimate are kept here, so a planner cannot forget them.
import type { Bbox, CamMove, CamPath } from './moves'
import type { Point3 } from './ops'
import { SESSION_UUID } from './path-uuid'

const ORIGIN: Point3 = { x: 0, y: 0, z: 0 }

/** The parser resolution of the file: a value is the same when it is this close. */
export const EPS = 1e-6

/** The cutting length of one move from `from`: the arc when it is an arc, the chord otherwise. */
export function moveLength(move: CamMove, from: Point3): number {
  if (move.kind !== 'cut') return 0
  if (move.arc !== undefined) return arcLength(move.arc.i, move.arc.j)
  const dx = move.to.x - from.x
  const dy = move.to.y - from.y
  const dz = move.to.z - from.z
  return Math.sqrt(dx * dx + dy * dy + dz * dz)
}

/**
 * The arc length of a half-turn arc from its centre offset, mm: the planner writes only half turns,
 * so the sweep is pi and the length is `pi * r`. One formula for the estimate and the cut length.
 */
export const arcLength = (i: number, j: number): number => Math.PI * Math.hypot(i, j)

/** The collector of one planner: the move list grows, the bounds and the cut length follow. */
export class PathBuilder {
  readonly moves: CamMove[] = []
  private readonly bbox: Bbox = {
    xMin: Infinity,
    xMax: -Infinity,
    yMin: Infinity,
    yMax: -Infinity,
    zMin: Infinity,
    zMax: -Infinity,
  }
  private lengthMm = 0

  /**
   * A cut carries its feed, a rapid carries none; both grow the bounds and the cut length. A move
   * to the point the tool already stands at is dropped: it is no move at all, and the demo's
   * counter (`FaceStats::moves`) charges only real travel.
   */
  add(kind: CamMove['kind'], to: Point3, feedMmMin?: number): void {
    if (this.samePoint(to)) return
    this.grow(to)
    const move: CamMove = kind === 'cut' ? { kind, to, feedMmMin: feedMmMin ?? 0 } : { kind, to }
    if (kind === 'cut') {
      this.lengthMm += moveLength(move, this.lastPoint())
    }
    this.moves.push(move)
  }

  /** An arc move: the cut length is the measured arc, not its chord. */
  addArc(move: CamMove): void {
    if (this.samePoint(move.to)) return
    this.grow(move.to)
    if (move.kind === 'cut') {
      this.lengthMm += moveLength(move, this.lastPoint())
    }
    this.moves.push(move)
  }

  /** The point the tool stands at before the next move; the origin before the first move. */
  private lastPoint(): Point3 {
    const last = this.moves[this.moves.length - 1]
    return last === undefined ? ORIGIN : last.to
  }

  /** True when the tool already stands at `to`: the first move has no predecessor and is kept. */
  private samePoint(to: Point3): boolean {
    const last = this.moves[this.moves.length - 1]
    return last !== undefined && last.to.x === to.x && last.to.y === to.y && last.to.z === to.z
  }

  /** The result of the planner: the moves, the box and the cut length. */
  result(): CamPath {
    const box = this.moves.length > 0 ? { ...this.bbox } : emptyBbox()
    return { moves: this.moves, bbox: box, lengthMm: this.lengthMm, cycleMinutes: 0, uuid: SESSION_UUID }
  }

  /** Grows the bounds with a point; an empty path keeps an inverted box instead of a fake zero. */
  grow(to: Point3): void {
    this.bbox.xMin = Math.min(this.bbox.xMin, to.x)
    this.bbox.xMax = Math.max(this.bbox.xMax, to.x)
    this.bbox.yMin = Math.min(this.bbox.yMin, to.y)
    this.bbox.yMax = Math.max(this.bbox.yMax, to.y)
    this.bbox.zMin = Math.min(this.bbox.zMin, to.z)
    this.bbox.zMax = Math.max(this.bbox.zMax, to.z)
  }
}

/** The box of a path without moves: every side is inverted, so the caller sees "no path". */
export const emptyBbox = (): Bbox => ({
  xMin: Infinity,
  xMax: -Infinity,
  yMin: Infinity,
  yMax: -Infinity,
  zMin: Infinity,
  zMax: -Infinity,
})
