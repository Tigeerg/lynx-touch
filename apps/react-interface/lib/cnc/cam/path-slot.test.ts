// The slot planner (PLAN-QUICKPROG, 7.6): a straight keyway, one pass per strip of the mill.
//
// A slot as wide as the mill is one pass down its centre. A wider one is cut by several passes
// across its width, each a straight run from `from` to `to` (the walk alternates), with the strips
// spreading from the centre so no band of the slot stays uncut. The passes go down by the axial step
// and the last is exactly the floor; every pass plunges at its strip and retracts to the top.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import type { Operation, Point3, SlotOperation } from './ops'
import { PathBuilder } from './path-builder'
import { planSlot, slotLayout } from './path-slot'
import { slotWarnings, validate } from './validate'

const p = (x: number, y: number, z = 0): Point3 => ({ x, y, z })
const SHAPE = { safeZ: 5, stock: { min: p(-50, -40, -20), max: p(50, 40, 0) } }

const slot = (patch: Partial<SlotOperation> = {}): SlotOperation => ({
  ...newOperation('slot', {
    id: 'Паз',
    topZ: 0,
    depthMm: 2,
    stepDownMm: 2,
    tool: { number: 1, diameterMm: 6, hNumber: 1 },
  }),
  from: p(-20, 0),
  to: p(20, 0),
  widthMm: 6,
  stepOverPct: 70,
  ...patch,
})

const plan = (op: SlotOperation) => {
  const out = new PathBuilder()
  planSlot(SHAPE, op, out)
  return out.moves
}

const fields = (op: Operation): string[] => {
  const checked = validate({ ...newProgram(), operations: [op] })
  return checked.ok ? [] : checked.bad.map((item) => item.field)
}

describe('slot as wide as the mill', () => {
  it('plans one pass down the centre of a 40 mm slot', () => {
    const layout = slotLayout(slot())
    expect(layout.passes).toEqual([-2])
    // A single strip is centred on the slot axis: one strip 6 mm wide, -20..20 along X, y stays 0.
    expect(layout.strips).toHaveLength(1)
    expect(layout.stepMm).toBe(0)
    expect([layout.strips[0].from.x, layout.strips[0].from.y]).toEqual([-20, 0])
    expect([layout.strips[0].to.x, layout.strips[0].to.y]).toEqual([20, 0])
    expect(layout.strips[0].acrossMm).toBe(0)
  })

  it('plunges at the start and retracts to the top at the end of the pass', () => {
    const moves = plan(slot())
    expect(moves.map((m) => [m.kind, m.to.x, m.to.y, m.to.z])).toEqual([
      ['rapid', -20, 0, 5],
      ['rapid', -20, 0, 0],
      ['cut', -20, 0, -2],
      ['cut', 20, 0, -2],
      ['cut', 20, 0, 0],
    ])
  })
})

describe('slot wider than the mill', () => {
  it('cuts a 15 mm slot of a 6 mm mill in four strips 3 mm apart, covering the width', () => {
    const layout = slotLayout(slot({ widthMm: 15 }))
    // usable = 15 - 6 = 9, nominal 6 * 0.7 = 4.2: ceil(9 / 4.2) + 1 = 4 strips, real step 3.
    expect(layout.strips.length).toBe(4)
    expect(layout.stepMm).toBeCloseTo(3, 9)
    const across = layout.strips.map((strip) => strip.acrossMm)
    // The strips spread evenly across the width: -4.5, -1.5, 1.5, 4.5.
    expect(across).toEqual([-4.5, -1.5, 1.5, 4.5])
    // The strips cover -7.5..7.5: the mill of radius 3 leaves no gap between two 3 mm neighbours.
    const half = 6 / 2
    expect(across[0] - half).toBeCloseTo(-7.5, 9)
    expect(across[across.length - 1] + half).toBeCloseTo(7.5, 9)
    for (let i = 1; i < across.length; i += 1) expect(across[i] - across[i - 1]).toBeLessThanOrEqual(6 + 1e-9)
  })

  it('walks the strips in alternating directions', () => {
    const moves = plan(slot({ widthMm: 15 }))
    // Four strips over a 9 mm-wide band: each strip plunges, runs to the floor and retracts, and the
    // walk turns at every strip.
    const floor = moves.filter((m) => m.kind === 'cut' && m.to.z === -2).map((m) => m.to.x)
    expect(floor).toEqual([-20, 20, 20, -20, -20, 20, 20, -20])
  })

  it('treats a width of exactly two diameters as strips, still inside the slot', () => {
    const layout = slotLayout(slot({ widthMm: 12 }))
    // usable = 6, nominal 4.2: ceil(6 / 4.2) + 1 = 3 strips, real step 3.
    expect(layout.strips.length).toBe(3)
    expect(layout.stepMm).toBeCloseTo(3, 9)
  })

  it('refuses a width narrower than the mill and warns over two diameters', () => {
    expect(fields(slot({ widthMm: 5 }))).toEqual(['widthMm'])
    const checked = validate({ ...newProgram(), operations: [slot({ widthMm: 5 })] })
    expect(checked.ok ? [] : checked.bad[0].reason).toContain('Паз уже фрезы')
    // A wider slot is not a document error (the save goes on); the planner warns instead.
    const wide = slot({ widthMm: 20 })
    expect(fields(wide)).toEqual([])
    expect(slotWarnings(wide)).toEqual([{ field: 'widthMm', reason: 'Паз шире двух диаметров: точнее контуром.' }])
    // Two diameters exactly is the widest slot that still passes without a warning.
    expect(slotWarnings(slot({ widthMm: 12 }))).toEqual([])
  })
})

describe('slot depth', () => {
  it('steps down 2.5 and lands on the floor 6 exactly', () => {
    const layout = slotLayout(slot({ depthMm: 6, stepDownMm: 2.5 }))
    expect(layout.passes).toEqual([-2.5, -5, -6])
  })

  it('cuts every pass at its Z and returns to the top between them', () => {
    const moves = plan(slot({ depthMm: 6, stepDownMm: 2.5 }))
    const depths = [...new Set(moves.filter((m) => m.kind === 'cut').map((m) => m.to.z))].sort((a, b) => b - a)
    expect(depths).toEqual([0, -2.5, -5, -6])
    const rapids = moves.filter((m) => m.kind === 'rapid')
    expect(rapids.every((m) => m.to.z >= 0)).toBe(true)
  })
})
