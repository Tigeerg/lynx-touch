// Counterboring (`counterbore`, PLAN-QUICKPROG 7.6): the G82 cycle, its dwell and its rules.
//
// The holes are the drilling ones (`drillGeometry`), so a grid counterbores the same centres it
// drills. G82 is G81 with a dwell P at the bottom of the hole: the tool waits `dwellSec` while the
// seat floor is cleaned, then retracts. The R plane is the drill's rule (a millimetre above the top,
// clamped to safe Z). The seat is a second diameter under the head: it must be at least the hole and
// not finer than its own depth, or the head of the fastener has nothing to sit on.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { formProblems } from './form-operation'
import { valuesOf } from './form'
import { buildProgram } from './path'
import { emitProgram } from './emit'
import { checkProgram } from './collision'
import type { CounterboreOperation, QuickProgram } from './ops'
import { serializeProgram, parseProgram } from './serialize'
import { validate } from './validate'

const stock = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

/** A counterbore with a 10 mm seat over a 6 mm hole, no grid by default. */
const counterbore = (patch: Partial<CounterboreOperation> = {}): CounterboreOperation => ({
  ...(newOperation('counterbore', { id: 'Цековка', topZ: 0, depthMm: 6 }) as CounterboreOperation),
  tool: { number: 1, diameterMm: 10, hNumber: 1 },
  hole: { x: 0, y: 0, r: 3 },
  seatDiameterMm: 10,
  seatDepthMm: 4,
  ...patch,
})

const program = (operations: QuickProgram['operations']): QuickProgram => ({
  ...newProgram(stock, 'CBORE'),
  operations,
})

const lines = (doc: QuickProgram): string[] => {
  const built = buildProgram(doc)
  if (!built.ok) throw new Error(built.problems.join('; '))
  return emitProgram(doc, built.path, checkProgram(doc, built.path).warnings)
}

const reasons = (doc: QuickProgram): { field: string; reason: string }[] => {
  const checked = validate(doc)
  return checked.ok ? [] : checked.bad
}

describe('цековка: цикл G82', () => {
  it('пишет сетку 2x2 четырьмя строками G82 с P0.2, R выше верха и G80', () => {
    // The seat is as deep as the head is wide: the shallow-seat warning stays out of the way here.
    const grid = { pattern: 'grid' as const, x0: -15, y0: -10, cols: 2, rows: 2, dx: 10, dy: 10 }
    const doc = program([counterbore({ ...grid, seatDepthMm: 10, depthMm: 10 })])
    const file = lines(doc)
    const cycles = file.filter((line) => line.includes('G82'))
    expect(cycles).toHaveLength(4)
    for (const cycle of cycles) expect(cycle).toContain('P0.2')
    const r = Number(/R(-?\d+\.\d+)/.exec(cycles[0] ?? '')?.[1])
    expect(r).toBeGreaterThan(0)
    // Every hole carries its own G82: the dwell is repeated, so the control never runs a bare move.
    expect(cycles.map((line) => /X(-?\d+\.\d+) Y(-?\d+\.\d+)/.exec(line)?.slice(1, 3))).toEqual([
      ['-15.000', '-10.000'],
      ['-5.000', '-10.000'],
      ['-15.000', '0.000'],
      ['-5.000', '0.000'],
    ])
    expect(file.some((line) => /^N\d+ G80$/.test(line))).toBe(true)
  })

  it('выдержка P берётся из опа, умолчание 0.2 с', () => {
    const plain = counterbore({ seatDepthMm: 10 })
    const quick = lines(program([plain]))
    expect(quick.find((line) => line.includes('G82'))).toContain('P0.2')
    const slow = lines(program([{ ...plain, dwellSec: 0.5 }]))
    expect(slow.find((line) => line.includes('G82'))).toContain('P0.5')
  })

  it('глубина цикла — верх минус глубина, R — миллиметр над верхом', () => {
    const file = lines(program([counterbore({ depthMm: 10 })]))
    const cycle = file.find((line) => line.includes('G82')) ?? ''
    expect(cycle).toContain('Z-10.000')
    expect(cycle).toContain('R1.000')
  })
})

describe('цековка: правила', () => {
  it('цековка уже отверстия — ошибка «Цековка уже отверстия»', () => {
    const doc = program([counterbore({ seatDiameterMm: 5, hole: { x: 0, y: 0, r: 3 } })])
    const bad = reasons(doc)
    expect(bad.some((item) => item.field === 'seatDiameterMm' && item.reason === 'Цековка уже отверстия')).toBe(true)
  })

  it('глубина цековки меньше диаметра — предупреждение, а не ошибка', () => {
    // The shallow seat reaches the form (`formProblems`) and the file; the document still plans.
    const doc = program([counterbore({ seatDepthMm: 4, seatDiameterMm: 10 })])
    const live = counterbore({ seatDepthMm: 4, seatDiameterMm: 10 })
    const shown = formProblems(doc, valuesOf(live), live).map((item) => item.reason)
    expect(shown).toContain('Цековка мельче диаметра: проверьте крепёж')
    const built = buildProgram(doc)
    if (!built.ok) throw new Error(built.problems.join('; '))
    const warnings = checkProgram(doc, built.path).warnings.map((item) => item.reason)
    expect(warnings).toContain('Цековка мельче диаметра: проверьте крепёж')
  })

  it('глубина цековки не больше нуля — ошибка', () => {
    const doc = program([counterbore({ seatDepthMm: 0 })])
    const bad = reasons(doc)
    expect(bad.some((item) => item.field === 'seatDepthMm')).toBe(true)
  })

  it('R обязателен: без hasR — ошибка поля rMm, даже при правдоподобном rMm', () => {
    // The plane is not optional: without it the cycle silently takes the default height. A number
    // in `rMm` that was never committed (`hasR` false) is not a plane either.
    const missing = reasons(program([counterbore({ hasR: false, rMm: 1 })]))
    expect(missing.some((item) => item.field === 'rMm')).toBe(true)
    expect(missing.find((item) => item.field === 'rMm')?.reason).toContain('плоскость R')
    // The frame of the form sets the plane (the check refuses a counterbore without it), so the
    // "no R" case is this one line of the file: the flag cleared, the number left as it was.
    const fromForm = newOperation('counterbore', { id: 'Цековка', topZ: 0, depthMm: 6 }) as CounterboreOperation
    expect(fromForm.hasR).toBe(true)
  })

  it('R ниже верха материала — ошибка поля rMm, заданное R выше верха допустимо', () => {
    const low = reasons(program([counterbore({ hasR: true, rMm: -2 })]))
    expect(low.some((item) => item.field === 'rMm')).toBe(true)
    const ok = reasons(program([counterbore({ hasR: true, rMm: 1 })]))
    expect(ok).toEqual([])
  })
})

describe('цековка: .qprog', () => {
  it('пишет op counterbore, op.seat, op.seatDepth, op.dwell и геометрию отверстий', () => {
    const doc = program([counterbore({ pattern: 'grid', x0: -15, y0: -10, cols: 2, rows: 2, dx: 10, dy: 10 })])
    const text = serializeProgram(doc)
    // The writer leaves `op.h` out when the row is the tool, as it does for every kind.
    const same = { ...counterbore({ pattern: 'grid', x0: -15, y0: -10, cols: 2, rows: 2, dx: 10, dy: 10 }) }
    same.tool = { ...same.tool, hNumber: 0 }
    expect(text).toContain('op counterbore "Цековка"')
    expect(text).toContain('op.seat 10\n')
    expect(text).toContain('op.seatDepth 4\n')
    expect(text).toContain('op.dwell 0.2\n')
    expect(text).toContain('op.grid -15 -10 2 2 10 10\n')
    const read = parseProgram(text)
    expect(read.ok).toBe(true)
    if (!read.ok) return
    expect(read.program.operations).toEqual([same])
  })
})
