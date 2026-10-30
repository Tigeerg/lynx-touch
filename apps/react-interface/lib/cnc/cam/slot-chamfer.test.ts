// The slot and chamfer operations end to end (PLAN-QUICKPROG, 7.6): the type, the form, the
// `.qprog` round trip and the emitted file.
//
// A slot is a straight keyway: one pass when the mill is as wide as the slot, several longitudinal
// passes when it is narrower. A chamfer is the mill running along an edge, sunk by the chamfer; only a
// straight edge is modelled. Both save through the same writer and read back through the strict
// reader, and both emit a file the viewer reads without an error.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { emitProgram } from './emit'
import { formOperation, formProblems, newForm, valuesOf } from './form'
import type { ChamferOperation, Operation, Point3, QuickProgram, SlotOperation } from './ops'
import { buildProgram } from './path'
import { parseProgram, serializeProgram } from './serialize'
import { validate } from './validate'
import { viewerErrors } from './viewer-check'

const STOCK = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }
const p = (x: number, y: number): Point3 => ({ x, y, z: 0 })

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

const program = (operations: Operation[]): QuickProgram => ({ ...newProgram(STOCK, 'Q4'), operations })

describe('slot and chamfer form', () => {
  it('maps a slot to the form values and back without losing the ends', () => {
    const op = slot({ from: p(-10, 2), to: p(10, 2), widthMm: 8 })
    const values = valuesOf(op)
    expect(values['from.x']).toBe('-10')
    expect(values['to.x']).toBe('10')
    expect(values.widthMm).toBe('8')
    expect(values.stepOverPct).toBe('70')
    const back = formOperation({ op: 'slot', values, error: null }, op)
    expect(back).toMatchObject({ kind: 'slot', from: op.from, to: op.to, widthMm: 8, stepOverPct: 70 })
  })

  it('maps a chamfer to the form values and back', () => {
    const op = chamfer({ chamferMm: 1.5 })
    const values = valuesOf(op)
    expect(values.chamferMm).toBe('1.5')
    const back = formOperation({ op: 'chamfer', values, error: null }, op)
    expect(back).toMatchObject({ kind: 'chamfer', from: op.from, to: op.to, chamferMm: 1.5 })
  })

  it('opens a new slot and a new chamfer form with the shape fields', () => {
    const slotForm = newForm('slot', newProgram(STOCK))
    expect(slotForm.values['from.x']).toBeDefined()
    expect(slotForm.values.widthMm).toBeDefined()
    const bad = { ...slotForm.values, widthMm: '2' }
    const op = formOperation({ ...slotForm, values: bad }, null)
    expect(formProblems(newProgram(STOCK), bad, op)[0].field).toBe('widthMm')
  })
})

describe('slot and chamfer .qprog', () => {
  it('writes and reads the slot keys', () => {
    const doc = program([slot({ widthMm: 8 })])
    const text = serializeProgram(doc)
    expect(text).toContain('op slot "Паз"')
    expect(text).toContain('op.from -20 0 0')
    expect(text).toContain('op.to 20 0 0')
    expect(text).toContain('op.width 8')
    expect(text).toContain('op.stepOver 70')
    const read = parseProgram(text)
    expect(read.ok).toBe(true)
    if (!read.ok) return
    expect(read.program.operations[0]).toMatchObject({ kind: 'slot', widthMm: 8, stepOverPct: 70 })
  })

  it('writes and reads the chamfer keys', () => {
    const doc = program([chamfer({ chamferMm: 1.5 })])
    const text = serializeProgram(doc)
    expect(text).toContain('op chamfer "Фаска"')
    expect(text).toContain('op.chamfer 1.5')
    const read = parseProgram(text)
    expect(read.ok).toBe(true)
    if (!read.ok) return
    expect(read.program.operations[0]).toMatchObject({ kind: 'chamfer', chamferMm: 1.5 })
  })

  it('names the missing width of a slot', () => {
    const text = serializeProgram(program([slot()])).replace('op.width 6\n', '')
    const read = parseProgram(text)
    expect(read.ok).toBe(false)
    if (read.ok) return
    expect(read.reason).toContain('op.width')
  })

  it('refuses a slot key on a chamfer', () => {
    const text = serializeProgram(program([chamfer()])).replace('op.chamfer 1\n', 'op.chamfer 1\nop.width 2\n')
    const read = parseProgram(text)
    expect(read.ok).toBe(false)
    if (read.ok) return
    expect(read.reason).toContain('op.width')
  })
})

describe('slot and chamfer emit', () => {
  it('writes a slot file the viewer reads without an error', () => {
    const doc = program([slot(), chamfer()])
    const built = buildProgram(doc)
    expect(built.ok).toBe(true)
    if (!built.ok) return
    const lines = emitProgram(doc, built.path)
    // The slot runs along X at the floor, the chamfer enters off the wall by D/3 (8 / 3 mm).
    expect(lines).toContain('N70 X20.000 F1432')
    expect(lines).toContain('N110 G0 X-15.000 Y-2.667')
    expect(viewerErrors(lines)).toEqual([])
    expect(validate(doc)).toEqual({ ok: true })
  })
})
