// The chamfer planner (PLAN-QUICKPROG, 7.6): take an edge off along a straight line.
//
// The mill runs along the edge on the given straight from `from` to `to`, sunk `chamferMm` below the
// top, so the corner of the tool takes off a triangle of the edge. It leads in along the normal of
// the edge by `D / 3` and out the same way, so the entry never drops onto the wall. The cut is split
// by the axial step from the top; the last pass is exactly `topZ - chamferMm`. Only a straight edge
// exists here: an arc of the edge is not modelled, and the corner of the mill is the chamfer itself.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import type { ChamferOperation, Operation, Point3 } from './ops'
import { PathBuilder } from './path-builder'
import { planChamfer, chamferLayout } from './path-chamfer'
import { validate } from './validate'

const p = (x: number, y: number, z = 0): Point3 => ({ x, y, z })
const SHAPE = { safeZ: 5, stock: { min: p(-50, -40, -20), max: p(50, 40, 0) } }

const chamfer = (patch: Partial<ChamferOperation> = {}): ChamferOperation => ({
  ...newOperation('chamfer', {
    id: 'Фаска',
    topZ: 0,
    depthMm: 1,
    stepDownMm: 1,
    tool: { number: 1, diameterMm: 8, hNumber: 1 },
  }),
  from: p(-15, 0),
  to: p(15, 0),
  chamferMm: 1,
  ...patch,
})

const plan = (op: ChamferOperation) => {
  const out = new PathBuilder()
  planChamfer(SHAPE, op, out)
  return out.moves
}

const fields = (op: Operation): string[] => {
  const checked = validate({ ...newProgram(), operations: [op] })
  return checked.ok ? [] : checked.bad.map((item) => item.field)
}

describe('chamfer planner', () => {
  it('walks a 30 mm edge of an 8 mm mill once, sunk 1 mm, entering off the wall by D/3', () => {
    const moves = plan(chamfer())
    // The edge runs +X: the left normal is +Y, so the lead goes to -Y, 8 / 3 mm off the wall.
    const cuts = moves.filter((m) => m.kind === 'cut')
    expect(cuts.map((m) => [m.to.x, m.to.y, m.to.z])).toEqual([
      [-15, -8 / 3, -1],
      [-15, 0, -1],
      [15, 0, -1],
      [15, -8 / 3, -1],
      [15, -8 / 3, 0],
    ])
  })

  it('plunges to the depth in one pass of a 1 mm chamfer', () => {
    const moves = plan(chamfer())
    const depths = [...new Set(moves.filter((m) => m.kind === 'cut').map((m) => m.to.z))]
    // The cut runs at -1 (the plunge, the edge ends and the rise all share the same Z of a pass),
    // plus the retract at the top; a 1 mm chamfer is a single pass.
    expect(depths).toEqual([-1, 0])
  })

  it('splits a deep chamfer by the axial step and lands exactly on the chamfer bottom', () => {
    // A 3 mm chamfer of an 8 mm mill by a 0.75 mm step: 0.75, 1.5, 2.25, 3.
    const layout = chamferLayout(chamfer({ chamferMm: 3, stepDownMm: 0.75 }))
    expect(layout.passes).toEqual([-0.75, -1.5, -2.25, -3])
    const below = plan(chamfer({ chamferMm: 3, stepDownMm: 0.75 }))
      .filter((m) => m.kind === 'cut' && m.to.z < 0)
      .map((m) => m.to.z)
    expect([...new Set(below)]).toEqual([-0.75, -1.5, -2.25, -3])
  })

  it('refuses a chamfer above the mill radius and a non-positive one', () => {
    expect(fields(chamfer({ chamferMm: 5 }))).toEqual(['chamferMm'])
    const checked = validate({ ...newProgram(), operations: [chamfer({ chamferMm: 5 })] })
    expect(checked.ok ? [] : checked.bad[0].reason).toContain('Фаска больше радиуса фрезы')
    expect(fields(chamfer({ chamferMm: 0 }))).toEqual(['chamferMm'])
    // Exactly the radius is the widest chamfer the corner can take.
    expect(fields(chamfer({ chamferMm: 4 }))).toEqual([])
  })
})
