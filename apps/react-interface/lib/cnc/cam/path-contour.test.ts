// The contour planner (PLAN-QUICKPROG, 7.6): the tool centre offset from the profile (C).
//
// The numbers are the ones a machinist checks with a ruler: the centre of an 8 mm mill cutting a
// 40 x 40 square from outside stands 4 mm off every side, so the tool path is a 36 x 36 square.
// On a corner the centre sits on the bisector, `4 / sin(45°)` from the vertex, whatever the angle.
// An open contour leads in and out along the normal; every Z pass is cut and the last is the floor.
import { describe, expect, it } from 'vitest'

import type { ContourOperation, Point3, ProgramShape } from './ops'
import { newOperation } from './defaults'
import { contourLayout, planContour } from './path-contour'
import { PathBuilder } from './path-builder'

const p = (x: number, y: number): Point3 => ({ x, y, z: 0 })

/** CCW square with corners (+-20, +-20): the material stands inside, left of the walk. */
const square = (): Point3[] => [p(-20, -20), p(20, -20), p(20, 20), p(-20, 20)]

const contour = (patch: Partial<ContourOperation> = {}): ContourOperation => ({
  ...newOperation('contour', {
    id: 'k',
    topZ: 0,
    depthMm: 2,
    stepDownMm: 2,
    tool: { number: 1, diameterMm: 8, hNumber: 1 },
  }),
  ...patch,
})

const cuts = (op: ContourOperation): Point3[] => {
  const out = new PathBuilder()
  planContour({ safeZ: 5, stock: { min: p(-50, -40), max: p(50, 40) } } as ProgramShape & { safeZ: number }, op, out)
  return out.moves.filter((m) => m.kind === 'cut').map((m) => m.to)
}

describe('contour offset', () => {
  it('shifts a CCW 40x40 square of an 8 mm mill in material to 36x36', () => {
    const op = contour({ points: square(), closed: true, allowanceMm: 0, side: 'material' })
    const corners = contourLayout(op).corners
    // The material is left of the walk; the centre moves right of it, i.e. into the square.
    expect(corners.map((c) => [c.x, c.y])).toEqual([
      [-16, -16],
      [16, -16],
      [16, 16],
      [-16, 16],
    ])
  })

  it('puts the centre 4 / sin 45 from a right-angled vertex', () => {
    const op = contour({ points: square(), closed: true, allowanceMm: 0, side: 'material' })
    const corner = contourLayout(op).corners[0]
    expect(Math.hypot(corner.x + 20, corner.y + 20)).toBeCloseTo(4 / Math.sin(Math.PI / 4), 9)
  })

  it('moves outward by D/2 + allowance on the air side', () => {
    const op = contour({ points: square(), closed: true, allowanceMm: 0.5, side: 'air' })
    const corners = contourLayout(op).corners.map((c) => [c.x, c.y])
    expect(corners).toEqual([
      [-24.5, -24.5],
      [24.5, -24.5],
      [24.5, 24.5],
      [-24.5, 24.5],
    ])
  })

  it('leads an open contour in and out along the normal at 30 % of the diameter', () => {
    const line = [p(0, 0), p(20, 0)]
    const op = contour({ points: line, closed: false, allowanceMm: 0, side: 'material' })
    const layout = contourLayout(op)
    // Walking +X, the material is at +Y: the centre is 4 mm above, the lead 2.4 mm along the same
    // normal. The entry is short of the start (the mill goes down off the wall), the exit past the end.
    expect(layout.corners.map((c) => [c.x, c.y])).toEqual([
      [0, 2.4],
      [20, 2.4],
    ])
    expect(layout.leadMm).toBeCloseTo(2.4, 9)
  })

  it('cuts one pass per step down and the last exactly at the floor', () => {
    const op = contour({ points: square(), closed: true, depthMm: 5, stepDownMm: 2 })
    const layout = contourLayout(op)
    expect(layout.passes).toEqual([-2, -4, -5])
    // Each pass: the plunge at the entry, the three other joints, the return to the entry and the
    // retract to the top. The retract may stand on the point the walk has already reached.
    const cut = cuts(op)
    expect(cut.length).toBe(6 * 3)
    expect(cut.filter((m) => m.z === -5).length).toBe(5)
    expect(cut.at(-1)?.z).toBe(0)
  })

  it('retracts to safe Z between passes and every rapid stays at or above the top', () => {
    const out = new PathBuilder()
    const op = contour({ points: square(), closed: true, depthMm: 5, stepDownMm: 2 })
    planContour({ safeZ: 5, stock: { min: p(-50, -40), max: p(50, 40) } } as ProgramShape & { safeZ: number }, op, out)
    const rapids = out.moves.filter((m) => m.kind === 'rapid')
    expect(rapids.every((m) => m.to.z >= 0)).toBe(true)
    expect(rapids.some((m) => m.to.z === 5)).toBe(true)
  })
})
