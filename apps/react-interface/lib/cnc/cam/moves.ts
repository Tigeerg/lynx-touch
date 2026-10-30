// The planner output: moves, not lines of G-code (PLAN-QUICKPROG, 6 and 7.4).
//
// The planner returns `CamPath`; `emit.ts` prints G-code and the preview draws the same moves, so
// the geometry has one source and two representations.
import type { Point3 } from './ops'

/** The bounding box of a path in machine coordinates, mm. */
export interface Bbox {
  xMin: number
  xMax: number
  yMin: number
  yMax: number
  zMin: number
  zMax: number
}

/** A move of the planner: a rapid or a cut at a feed, with an optional arc centre (I/J). */
export interface CamMove {
  kind: 'rapid' | 'cut'
  to: Point3
  feedMmMin?: number
  arc?: { cw: boolean; i: number; j: number }
}

/** The result of the planner: moves, not lines of G-code, with the box and the estimates. */
export interface CamPath {
  moves: CamMove[]
  bbox: Bbox
  /** Cutting length, mm. */
  lengthMm: number
  /** Estimated cutting and travel time, minutes. */
  cycleMinutes: number
  /** The plan identity: `<uuid>/op<n>` names a stage, so a stale preview can be told from a fresh one. */
  uuid: string
}
