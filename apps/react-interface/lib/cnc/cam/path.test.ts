// The planner properties of the four kinds (PLAN-QUICKPROG, 10, C2).
//
// The golden files of `pipeline.test.ts` prove one set of parameters word for word; these cases
// probe the rules that are easy to lose outside that set: the sorted stock, the face lead and its
// cap, the pass depth, the pocket inset and the spiral of a single turn.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import type { CirclePocketOperation, FaceOperation, Operation, RectPocketOperation, StockBox } from './ops'
import { axialPasses, faceLayout } from './path-face'
import { pocketLayout } from './path-pocket'
import { circleLayout } from './path-circle-pocket'
import { drillCycle, drillPlan } from './path-drill'
import { buildProgram } from './path'
import { stockTop } from './ops'

const stock = (): StockBox => ({ min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } })

const face = (patch: Partial<FaceOperation> = {}): FaceOperation => ({
  ...newOperation('face', { id: 'face', topZ: 0, depthMm: 2, stepDownMm: 2 }),
  ...patch,
})

const pocket = (patch: Partial<RectPocketOperation> = {}): RectPocketOperation => ({
  ...newOperation('pocket', { id: 'pocket', topZ: 0, depthMm: 4, stepDownMm: 2 }),
  rect: { x: -20, y: -10, w: 40, h: 20 },
  ...patch,
})

const circle = (patch: Partial<CirclePocketOperation> = {}): CirclePocketOperation => ({
  ...newOperation('circle-pocket', { id: 'circle', topZ: 0, depthMm: 4, stepDownMm: 2 }),
  circle: { x: 0, y: 0, r: 10 },
  ...patch,
})

const plan = (operations: Operation[]) => {
  const program = { ...newProgram(stock(), 'PROBE'), operations }
  const built = buildProgram(program)
  if (!built.ok) throw new Error(built.problems.join('; '))
  return { program, path: built.path }
}

describe('axial passes', () => {
  it('steps down and lands on the bottom exactly, never below it', () => {
    expect(axialPasses(0, 5, 2)).toEqual([-2, -4, -5])
    expect(axialPasses(0, 5, 5)).toEqual([-5])
    expect(axialPasses(0, 5, 0)).toEqual([-5])
  })
})

describe('face layout', () => {
  it('spans the stock with a real step no wider than the nominal one', () => {
    const layout = faceLayout(stock(), face({ tool: { number: 1, diameterMm: 10, hNumber: 1 }, stepOverPct: 70 }))
    // usable = 80 - 10 = 70, nominal 7: 11 strips, a real step of 7 exactly.
    expect(layout.strips).toBe(11)
    expect(layout.stripStepMm).toBeCloseTo(7, 9)
    expect(layout.alongX).toBe(true)
    expect(layout.edgeOverrunMm).toBe(1)
  })

  it('keeps the real step under a nominal one that does not divide the span', () => {
    const layout = faceLayout(stock(), face({ tool: { number: 1, diameterMm: 12, hNumber: 1 }, stepOverPct: 60 }))
    expect(layout.strips).toBe(11)
    expect(layout.stripStepMm).toBeLessThanOrEqual(12 * 0.6 + 1e-9)
  })

  it('gives a tool wider than the stock one strip through the centre', () => {
    const narrow: StockBox = { min: { x: -10, y: -4, z: -5 }, max: { x: 10, y: 4, z: 0 } }
    const layout = faceLayout(narrow, face({ tool: { number: 1, diameterMm: 10, hNumber: 1 } }))
    expect(layout.strips).toBe(1)
    expect(layout.stripStepMm).toBe(0)
  })

  it('never lets a rapid go below the material top', () => {
    const { path } = plan([face()])
    for (const move of path.moves) {
      if (move.kind === 'rapid') expect(move.to.z).toBeGreaterThanOrEqual(0)
    }
  })

  it('cuts the last pass exactly at the bottom', () => {
    const { path } = plan([face({ depthMm: 5, stepDownMm: 2 })])
    const depths = path.moves.filter((move) => move.kind === 'cut').map((move) => move.to.z)
    expect(Math.min(...depths)).toBe(-5)
  })
})

describe('rectangular pocket', () => {
  it('keeps the tool body inside the wall by the inset', () => {
    const op = pocket({ tool: { number: 1, diameterMm: 8, hNumber: 1 }, finishAllowanceMm: 0.5 })
    const layout = pocketLayout(op)
    expect(layout.fits).toBe(true)
    expect(layout.insetMm).toBe(4.5)
    expect(layout.acrossMin).toBe(-10 + 4.5)
    expect(layout.acrossMax).toBe(10 - 4.5)
  })

  it('refuses to invent a path when the tool does not fit', () => {
    const op = pocket({ tool: { number: 1, diameterMm: 30, hNumber: 1 }, rect: { x: 0, y: 0, w: 20, h: 10 } })
    expect(pocketLayout(op).fits).toBe(false)
    // The check names the field first; the planner refuses the path and never throws.
    const built = buildProgram({ ...newProgram(stock(), 'PROBE'), operations: [op] })
    expect(built.ok).toBe(false)
  })

  it('cuts the floor to the final depth in the same pass', () => {
    const { path } = plan([pocket({ depthMm: 5, stepDownMm: 2 })])
    const depths = path.moves.filter((move) => move.kind === 'cut').map((move) => move.to.z)
    expect(Math.min(...depths)).toBe(-5)
  })
})

describe('round pocket', () => {
  it('keeps the outermost turn the allowance away from the wall', () => {
    const op = circle({ tool: { number: 1, diameterMm: 8, hNumber: 1 }, finishAllowanceMm: 0.3 })
    const layout = circleLayout(op)
    expect(layout.radiusMm).toBeCloseTo(10 - 4.3, 9)
    expect(layout.spiral[layout.spiral.length - 1].radiusMm).toBeCloseTo(layout.radiusMm, 9)
  })

  it('writes every arc with an end point and the centre offset of its start', () => {
    const { path } = plan([circle()])
    const arcs = path.moves.filter((move) => move.arc !== undefined)
    expect(arcs.length).toBeGreaterThan(0)
    for (const move of arcs) {
      expect(move.kind).toBe('cut')
      expect(Math.hypot(move.arc!.i, move.arc!.j)).toBeGreaterThan(0)
    }
  })
})

describe('drilling', () => {
  it('peeks a hole deeper than three diameters with a Q of one diameter', () => {
    const { program } = plan([newOperation('drill', { id: 'd', topZ: 0, depthMm: 20 })])
    const op = program.operations[0]
    if (op.kind !== 'drill') throw new Error('not a drill')
    const cycle = drillCycle(program, { ...op, tool: { number: 1, diameterMm: 5, hNumber: 1 } })
    expect(cycle.peck).toBe(true)
    expect(cycle.peckMm).toBe(5)
  })

  it('drills a shallow hole in one go and keeps the R plane above the material', () => {
    const { program } = plan([newOperation('drill', { id: 'd', topZ: 0, depthMm: 3 })])
    const op = program.operations[0]
    if (op.kind !== 'drill') throw new Error('not a drill')
    const planD = drillPlan(program, op)
    expect(planD.cycle.peck).toBe(false)
    expect(planD.cycle.rZ).toBeGreaterThan(planD.planes.topZ)
    expect(planD.cycle.rZ).toBeLessThanOrEqual(program.safeZ)
  })

  it('plans one rapid per hole and no cut', () => {
    const { program, path } = plan([newOperation('drill', { id: 'd', topZ: 0, depthMm: 3 })])
    expect(path.moves.length).toBe(1)
    expect(path.moves[0].kind).toBe('rapid')
    expect(path.moves[0].to.z).toBe(stockTop(program.stock) + 5)
  })
})
