// The probe block (PLAN-QUICKPROG, 7.6): G38.2 per measurement, the retreat after each touch, the
// oross checks of the range and the touch feed, and the `.qprog` round trip.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { probeLines, probeValidation, probeWarnings } from './probe'
import type { Operation } from './ops'
import { parseProgram, serializeProgram } from './serialize'
import { buildProgram } from './path'
import { emitProgram } from './emit'
import { checkProgram } from './collision'
import { validate } from './validate'
import { viewerErrors } from './viewer-check'

const probe = (patch: Partial<Extract<Operation, { kind: 'probe' }>> = {}): Operation => ({
  ...newOperation('probe', { id: 'Щуп', topZ: 0 }),
  points: [{ x: 10, y: 20, z: -50 }],
  rangeMm: 30,
  touchFeedMmMin: 900,
  axis: 'Z',
  ...patch,
})

const program = (operations: Operation[]) => ({ ...newProgram(), operations })

describe('probe: вывод G38.2', () => {
  it('каждое измерение — G38.2 по оси с подачей касания и отвод', () => {
    const lines = probeLines(probe() as Extract<Operation, { kind: 'probe' }>)
    expect(lines).toEqual(['G38.2 Z-50.000 F900', 'G0 Z-20.000'])
  })

  it('несколько точек — по блоку на точку, отвод после каждого касания', () => {
    const op = probe({
      points: [
        { x: 0, y: 0, z: -80 },
        { x: 1, y: 1, z: -80 },
      ],
    }) as Extract<Operation, { kind: 'probe' }>
    const lines = probeLines(op)
    expect(lines).toEqual(['G38.2 Z-80.000 F900', 'G0 Z-50.000', 'G38.2 Z-80.000 F900', 'G0 Z-50.000'])
  })
})

describe('probe: проверки', () => {
  it('пустой список точек — ошибка', () => {
    const op = probe({ points: [] }) as Extract<Operation, { kind: 'probe' }>
    expect(probeValidation(op).map((item) => item.field)).toContain('points')
  })

  it('диапазон не больше нуля — ошибка', () => {
    const op = probe({ rangeMm: 0 }) as Extract<Operation, { kind: 'probe' }>
    expect(probeValidation(op).map((item) => item.field)).toContain('rangeMm')
  })

  it('подача касания выше рабочей — предупреждение', () => {
    const op = probe() as Extract<Operation, { kind: 'probe' }>
    expect(probeValidation(op)).toEqual([])
    expect(probeWarnings(op)).toHaveLength(1)
    expect(probeWarnings(probe({ touchFeedMmMin: 50 }) as Extract<Operation, { kind: 'probe' }>)).toEqual([])
  })

  it('точка не числом — ошибка в поле points', () => {
    const op = probe({ points: [{ x: Number.NaN, y: 0, z: -10 }] }) as Extract<Operation, { kind: 'probe' }>
    expect(probeValidation(op).map((item) => item.field)).toContain('points')
  })
})

describe('probe: .qprog', () => {
  it('round-trip probe: op probe, points, range, touch, axis', () => {
    const op = probe({ points: [{ x: 1.5, y: -2.5, z: -30 }] })
    const text = serializeProgram(program([op]))
    expect(text).toContain('op probe "Щуп"')
    expect(text).toContain('op.points 1.5 -2.5 -30')
    expect(text).toContain('op.range 30')
    expect(text).toContain('op.touch 900')
    expect(text).toContain('op.axis Z')
    const read = parseProgram(text)
    expect(read.ok).toBe(true)
    if (!read.ok) return
    const back = read.program.operations[0]
    expect(back.kind).toBe('probe')
    if (back.kind !== 'probe') return
    expect(back.points).toEqual([{ x: 1.5, y: -2.5, z: -30 }])
    expect(back.rangeMm).toBe(30)
    expect(back.touchFeedMmMin).toBe(900)
    expect(back.axis).toBe('Z')
  })
})

describe('probe: файл', () => {
  it('блок щупа в УП: UI_OP, UI_TOOL с PROBE, G38.2 и отвод, без ошибок вьювера', () => {
    const op = probe()
    const doc = program([op])
    const built = buildProgram(doc)
    if (!built.ok) throw new Error(built.problems.join('; '))
    const lines = emitProgram(doc, built.path, checkProgram(doc, built.path).warnings)
    const text = lines.join('\n')
    expect(text).toContain('(UI_OP: Щуп)')
    expect(text).toMatch(/\(UI_TOOL: T=\d+ TYPE=PROBE/)
    expect(lines.some((line) => /N\d+ G38\.2 Z-50\.000 F900/.test(line))).toBe(true)
    expect(lines.some((line) => /N\d+ G0 Z-20\.000/.test(line))).toBe(true)
    expect(viewerErrors(lines)).toEqual([])
    // The block has no cut, so it adds no warning either: the only finding of the file is the park.
    expect(checkProgram(doc, built.path).errors).toEqual([])
  })

  it('пустой список точек не сохраняется: validate называет поле points', () => {
    const checked = validate(program([probe({ points: [] })]))
    expect(checked.ok).toBe(false)
    if (checked.ok) return
    expect(checked.bad.map((item) => item.field)).toContain('points')
  })
})
