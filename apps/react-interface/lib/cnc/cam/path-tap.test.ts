// Tapping (`tap`, PLAN-QUICKPROG 7.6) and the free G-code block (`custom`): the numbers, the G84
// cycle and the viewer parser.
//
// The hole layout is the drilling one (`drill-holes.ts`), so a grid and a ring tap the same centres
// they drill. The feed of G84 is the pitch times the spindle speed: the tool advances one pitch per
// turn, and the rigid tap returns at the same feed. A custom block is emitted as the typed lines;
// the viewer parser is the only check it gets.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { buildProgram } from './path'
import { emitProgram } from './emit'
import { checkProgram } from './collision'
import type { CustomOperation, QuickProgram, TapOperation } from './ops'
import { defaultParams, paramValue } from '../service-data'
import { RIGID_TAPPING_PARAM } from './validate-tap'
import { validate } from './validate'
import { viewerErrors, viewerIssues } from './viewer-check'
import { parseProgram, serializeProgram } from './serialize'

const stock = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

/** A program of the given operations with the machine parameters of the panel. */
const program = (operations: QuickProgram['operations'], params: Record<string, number> = {}): QuickProgram => ({
  ...newProgram(stock, 'TAP'),
  operations,
  params: { ...params },
})

/** A tap with a 3 x 4 grid of holes, 10 mm apart, a grid inside the stock. */
const tap = (patch: Partial<TapOperation> = {}): TapOperation => ({
  ...(newOperation('tap', { id: 'Метчик', topZ: 0, depthMm: 8 }) as TapOperation),
  pattern: 'grid',
  x0: -15,
  y0: -10,
  cols: 3,
  rows: 4,
  dx: 10,
  dy: 10,
  pitchMm: 1.25,
  cutting: { spindleRpm: 400, feedMmMin: 500, plungeFeedMmMin: 500 },
  ...patch,
})

const lines = (doc: QuickProgram): string[] => {
  const built = buildProgram(doc)
  if (!built.ok) throw new Error(built.problems.join('; '))
  return emitProgram(doc, built.path, checkProgram(doc, built.path).warnings)
}

describe('метчик: строка G84', () => {
  it('пишет сетку 3x4 подачей шаг x обороты, R выше верха и G80', () => {
    const doc = program([tap()], { [RIGID_TAPPING_PARAM]: 1 })
    const file = lines(doc)
    const cycle = file.find((line) => line.includes('G84'))
    expect(cycle).toBeDefined()
    // pitch 1.25 mm/rev x 400 rpm = 500 mm/min, the same number for any hole of the grid.
    expect(cycle).toContain('F500')
    expect(cycle).toContain('X-15.000 Y-10.000')
    const r = Number(/R(-?\d+\.\d+)/.exec(cycle ?? '')?.[1])
    expect(r).toBeGreaterThan(0)
    // The first hole carries G84, the other eleven only move; G80 closes the cycle.
    expect(file.filter((line) => /^N\d+ X-?\d+\.\d+ Y-?\d+\.\d+$/.test(line))).toHaveLength(11)
    expect(file.some((line) => /^N\d+ G80$/.test(line))).toBe(true)
  })

  it('подача считается по шагу и оборотам, а не по полю подачи', () => {
    const doc = program([tap({ pitchMm: 2, cutting: { spindleRpm: 300, feedMmMin: 10, plungeFeedMmMin: 10 } })], {
      [RIGID_TAPPING_PARAM]: 1,
    })
    expect(lines(doc).find((line) => line.includes('G84'))).toContain('F600')
  })

  it('время цикла считает реверс: врезание и обратный ход на шаг x обороты', () => {
    const doc = program([tap()], { [RIGID_TAPPING_PARAM]: 1 })
    const built = buildProgram(doc)
    if (!built.ok) throw new Error('no plan')
    // 12 holes x 8 mm out and back at 500 mm/min = 12 * 16 / 500 min; the rigid tap does not peck.
    expect(built.path.cycleMinutes).toBeCloseTo((12 * 2 * 8) / 500, 9)
  })
})

describe('метчик: проверки', () => {
  it('без жёсткого нарезания станок не режет резьбу: ошибка с полем', () => {
    const checked = validate(program([tap()], { [RIGID_TAPPING_PARAM]: 0 }))
    expect(checked.ok).toBe(false)
    if (checked.ok) throw new Error('valid')
    expect(checked.bad[0]?.field).toBe('rigidTapping')
    expect(checked.bad[0]?.reason).toContain('жёсткого нарезания')
  })

  it('панель с параметром станка по умолчанию, без правки настроек, не пишет G84', () => {
    const checked = validate(program([tap()], { ...defaultParams() }))
    expect(checked.ok).toBe(false)
    if (checked.ok) throw new Error('valid')
    expect(checked.bad[0]?.field).toBe('rigidTapping')
  })

  it('параметр станка по умолчанию выключен', () => {
    expect(paramValue({}, RIGID_TAPPING_PARAM)).toBe(0)
  })

  it('шаг резьбы не больше нуля — ошибка', () => {
    const checked = validate(program([tap({ pitchMm: 0 })], { [RIGID_TAPPING_PARAM]: 1 }))
    expect(checked.ok).toBe(false)
    if (checked.ok) throw new Error('valid')
    expect(checked.bad.map((item) => item.field)).toContain('pitchMm')
  })

  it('нестандартный шаг — предупреждение, а не ошибка', () => {
    const doc = program([tap({ pitchMm: 1.1 })], { [RIGID_TAPPING_PARAM]: 1 })
    expect(validate(doc)).toEqual({ ok: true })
    const built = buildProgram(doc)
    if (!built.ok) throw new Error('no plan')
    const warnings = checkProgram(doc, built.path).warnings
    expect(warnings.some((item) => item.reason.startsWith('Нестандартный шаг резьбы'))).toBe(true)
  })
})

describe('Custom G Code', () => {
  // A block with a tool of the table, as the form of a real part has: the tool is optional, not absent.
  const custom = (gcode: string[]): CustomOperation => ({
    ...(newOperation('custom', { id: 'Свой код', topZ: 0 }) as CustomOperation),
    tool: { number: 5, diameterMm: 6, hNumber: 5 },
    gcode,
  })

  it('строки стоят в файле после UI_OP и UI_TOOL, вьювер ошибок не видит', () => {
    const doc = program([custom(['(комментарий)', 'G0 X10'])])
    const file = lines(doc)
    const op = file.findIndex((line) => line.includes('UI_OP'))
    const tool = file.findIndex((line) => line.includes('UI_TOOL'))
    const comment = file.indexOf('(комментарий)')
    const move = file.findIndex((line) => line.includes('G0 X10'))
    expect(op).toBeGreaterThan(-1)
    expect(tool).toBeGreaterThan(op)
    expect(comment).toBeGreaterThan(tool)
    expect(move).toBeGreaterThan(comment)
    expect(viewerErrors(file)).toEqual([])
    expect(viewerIssues(file).some((item) => item.code === 'op-no-tool')).toBe(false)
  })

  it('ошибка парсера вьювера (дуга без центра) показывает предупреждение в файле', () => {
    const doc = program([custom(['G0 X0', 'G3 I5'])])
    const file = lines(doc)
    // The free block reaches the file as the operator typed it, so the parser finds the fault there.
    expect(viewerErrors(file).some((item) => item.code === 'arc-no-end')).toBe(true)
    expect(viewerIssues(file).some((item) => item.code === 'arc-no-end')).toBe(true)
  })

  it('пустой список строк — ошибка, комментарий без слов допустим', () => {
    const empty = validate(program([custom([])]))
    expect(empty.ok).toBe(false)
    if (empty.ok) throw new Error('valid')
    expect(empty.bad[0]?.field).toBe('gcode')
    expect(validate(program([custom(['(только комментарий)'])]))).toEqual({ ok: true })
  })

  it('у операции без инструмента нет смены и нет UI_TOOL, вьювер предупреждает', () => {
    const op = newOperation('custom', { id: 'Код', topZ: 0 }) as CustomOperation
    op.gcode = ['G0 X1']
    op.tool = { number: 0, diameterMm: 0, hNumber: 0 }
    const doc = program([op])
    const file = lines(doc)
    expect(file.some((line) => line.includes('M6'))).toBe(false)
    expect(file.some((line) => line.includes('UI_TOOL'))).toBe(false)
    expect(viewerIssues(file).some((item) => item.code === 'op-no-tool')).toBe(true)
  })
})

describe('метчик и custom в .qprog', () => {
  it('метчик переживает запись и чтение: схема, шаг и геометрия отверстий', () => {
    const doc = program([tap({ pattern: 'circle', count: 6, pitchMm: 1.5 })], { [RIGID_TAPPING_PARAM]: 1 })
    const text = serializeProgram(doc)
    expect(text).toContain('op tap "Метчик"')
    expect(text).toContain('op.pattern circle')
    expect(text).toContain('op.pitch 1.5')
    const back = parseProgram(text)
    if (!back.ok) throw new Error(back.reason)
    const op = back.program.operations[0]
    expect(op?.kind).toBe('tap')
    if (op?.kind !== 'tap') throw new Error('not a tap')
    expect(op.pattern).toBe('circle')
    expect(op.pitchMm).toBe(1.5)
    expect(op.count).toBe(6)
    expect(op.hole).toEqual(tap().hole)
  })

  it('custom пишет строку на строку `op.gcode` и читает список без потерь', () => {
    const code = ['(комментарий)', 'G0 X10', '', 'G1 Z-1 F100']
    const block = { ...newOperation('custom', { id: 'Код', topZ: 0 }), gcode: code }
    const text = serializeProgram(program([block]))
    // A quoted text as `op.notes`; the empty line is the bare key, so the list keeps its blanks.
    expect(text).toContain('op.gcode "(комментарий)"')
    expect(text).toContain('op.gcode "G0 X10"')
    expect(text).toContain('\nop.gcode\n')
    const back = parseProgram(text)
    if (!back.ok) throw new Error(back.reason)
    const op = back.program.operations[0]
    expect(op?.kind).toBe('custom')
    if (op?.kind !== 'custom') throw new Error('not a custom block')
    expect(op.gcode).toEqual(code)
  })
})
