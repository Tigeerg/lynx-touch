// Thread milling (`thread`, PLAN-QUICKPROG 7.6): the helix, its radius and the passes.
//
// The mill cuts a thread by walking a helix: one turn climbs one pitch, so the helix radius is the
// difference between the thread radius and the mill radius. An internal M8x1.25 with a 6 mm mill
// turns on a 1 mm radius (`4 - 3`): the outer point of the mill is the 4 mm thread crest. An
// external M10x1.5 with the same mill turns on 8 mm (`5 + 3`): the inner point is the 5 mm flank.
// The turns are `threadDepth / pitch`; a whole turn is two half arcs (the arc rule of 7.1), and the
// last radial pass is the finish with no stock.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { buildProgram } from './path'
import { emitProgram } from './emit'
import { checkProgram } from './collision'
import type { QuickProgram, ThreadOperation } from './ops'
import { PathBuilder } from './path-builder'
import { planThread, threadLayout } from './path-thread'
import { serializeProgram, parseProgram } from './serialize'
import { validate } from './validate'
import { formOperation, newForm, valuesOf } from './form'
import type { CamForm } from './form-fields'

const stock = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

/** M8x1.25 internal: the thread radius 4 mm, the mill 6 mm, the thread 10 mm long. */
const internal = (patch: Partial<ThreadOperation> = {}): ThreadOperation => ({
  ...(newOperation('thread', { id: 'Резьба', topZ: 0, depthMm: 10 }) as ThreadOperation),
  tool: { number: 1, diameterMm: 6, hNumber: 1 },
  hole: { x: 0, y: 0, r: 4 },
  pitchMm: 1.25,
  internal: true,
  threadDepthMm: 10,
  threadAllowanceMm: 0,
  ...patch,
})

const program = (operations: QuickProgram['operations']): QuickProgram => ({
  ...newProgram(stock, 'THREAD'),
  operations,
})

const plan = (op: ThreadOperation): ReturnType<PathBuilder['result']> => {
  const out = new PathBuilder()
  planThread(program([op]), op, out)
  return out.result()
}

const reasons = (doc: QuickProgram): { field: string; reason: string }[] => {
  const checked = validate(doc)
  return checked.ok ? [] : checked.bad
}

describe('резьба: поле internal в форме', () => {
  it('новая наружная резьба: internal в форме off, операция наружная', () => {
    // The form of a new thread opens on the document default (internal); the operator clears the
    // flag for an external thread, and the cleared box must reach the operation, not be spelled out
    // again: the field is a switch, not a default the form forgets.
    const base = newForm('thread', newProgram(stock, 'THREAD'))
    expect(base.values.internal).toBe('on')
    const external: CamForm = { ...base, values: { ...base.values, internal: 'off' } }
    const op = formOperation(external, null)
    expect(op.kind === 'thread' && op.internal).toBe(false)
    expect(valuesOf(op).internal).toBe('off')
  })

  it('редактирование наружной резьбы сохраняет internal false', () => {
    const out = internal({ internal: false, tool: { number: 1, diameterMm: 4, hNumber: 1 } })
    const back = formOperation({ op: 'thread', values: valuesOf(out), error: null }, out)
    expect(back.kind === 'thread' && back.internal).toBe(false)
    expect(valuesOf(back).internal).toBe('off')
  })
})

describe('резьбовая фреза: радиус спирали', () => {
  it('M8x1.25 внутренняя фрезой 6: радиус 4 - 3 = 1 мм, витков threadDepth / pitch', () => {
    const layout = threadLayout(internal())
    expect(layout.radiusMm).toBeCloseTo(1, 9)
    expect(layout.turns).toBeCloseTo(10 / 1.25, 9)
    expect(layout.turns).toBe(8)
  })

  it('M10x1.5 наружная фрезой 6: радиус 5 + 3 = 8 мм', () => {
    const layout = threadLayout(internal({ internal: false, hole: { x: 0, y: 0, r: 5 }, pitchMm: 1.5 }))
    expect(layout.radiusMm).toBeCloseTo(8, 9)
  })

  it('полный круг — две полудуги: внутренняя идёт G2, наружная G3', () => {
    const inner = plan(internal()).moves.filter((move) => move.arc !== undefined)
    // One turn is two half arcs; the first two are one turn.
    expect(inner.length).toBeGreaterThanOrEqual(2)
    expect(inner[0].arc?.cw).toBe(true)
    expect(inner[1].arc?.cw).toBe(true)
    // The helix starts at the top point (+Y) and climbs one pitch per turn.
    expect(inner[0].to.y).toBeCloseTo(-1, 9)
    expect(inner[1].to.y).toBeCloseTo(1, 9)
    const outer = plan(internal({ internal: false, hole: { x: 0, y: 0, r: 5 }, pitchMm: 1.5 }))
    const arcs = outer.moves.filter((move) => move.arc !== undefined)
    expect(arcs[0].arc?.cw).toBe(false)
  })

  it('спираль поднимается на шаг за оборот: две полудуги = 1.25 мм', () => {
    const moves = plan(internal()).moves.filter((move) => move.arc !== undefined)
    // Every half arc climbs half a pitch: the plunge happened on the start point before them.
    expect(moves[0].to.z).toBeCloseTo(-0.625, 9)
    expect(moves[1].to.z).toBeCloseTo(-1.25, 9)
    expect(moves.length).toBe(16)
  })

  it('окружность дуги — радиус спирали, I/J от начала дуги к центру', () => {
    const moves = plan(internal()).moves.filter((move) => move.arc !== undefined)
    const first = moves[0]
    expect(Math.hypot(first.arc?.i ?? 0, first.arc?.j ?? 0)).toBeCloseTo(1, 9)
  })

  it('чистовой проход идёт по радиусу с припуском 0, черновой — снаружи него', () => {
    const plain = threadLayout(internal())
    expect(plain.passes).toEqual([1])
    const rough = threadLayout(internal({ threadAllowanceMm: 0.3, tool: { number: 1, diameterMm: 6, hNumber: 1 } }))
    // The step is 6 * 70 % = 4.2 mm, so the 0.3 allowance is one rough pass; the finish is the last.
    expect(rough.passes).toEqual([1.3, 1])
  })

  it('пишет дуги с I/J и подачей; наружная резьба — G3', () => {
    const file = (op: ThreadOperation): string[] => {
      const doc = program([op])
      const built = buildProgram(doc)
      if (!built.ok) throw new Error(built.problems.join('; '))
      return emitProgram(doc, built.path, checkProgram(doc, built.path).warnings)
    }
    // G2 is modal: the first half arc writes it, the next arcs of the same direction need no word.
    const inner = file(internal())
    expect(inner.some((line) => /\bG2\b/.test(line))).toBe(true)
    expect(inner.some((line) => line.includes('I0.000') && line.includes('J-1.000'))).toBe(true)
    const outer = file(internal({ internal: false, hole: { x: 0, y: 0, r: 5 }, pitchMm: 1.5 }))
    expect(outer.some((line) => /\bG3\b/.test(line))).toBe(true)
    expect(outer.some((line) => /\bG2\b/.test(line))).toBe(false)
  })
})

describe('резьбовая фреза: правила', () => {
  it('шаг не больше нуля и шаг больше инструмента — ошибка', () => {
    expect(reasons(program([internal({ pitchMm: 0 })])).some((item) => item.field === 'pitchMm')).toBe(true)
    expect(reasons(program([internal({ pitchMm: 8 })])).some((item) => item.field === 'pitchMm')).toBe(true)
  })

  it('внутренняя: фреза не проходит в отверстие — ошибка', () => {
    // D = 6, thread radius 2.5: 5 / 2 = 2.5 < 2.5 - 0.625 — the mill body eats the flank.
    const doc = program([internal({ hole: { x: 0, y: 0, r: 2.5 } })])
    const bad = reasons(doc)
    expect(bad.some((item) => item.reason === 'Фреза не проходит в отверстие под резьбу')).toBe(true)
  })

  it('наружная: фреза тоньше впадины — ошибка', () => {
    // D = 6, thread diameter 6, pitch 1.5: 6 <= 6 - 1.5 is false.
    const doc = program([internal({ internal: false, hole: { x: 0, y: 0, r: 3 }, pitchMm: 1.5 })])
    expect(reasons(doc).some((item) => item.field === 'tool.diameterMm')).toBe(true)
  })
})

describe('резьбовая фреза: .qprog', () => {
  it('пишет op thread, op.pitch, op.internal, op.threadDepth и читает обратно', () => {
    // The writer leaves `op.h` out when the row is the tool, as it does for every kind.
    const doc = program([internal()])
    const text = serializeProgram(doc)
    expect(text).toContain('op thread "Резьба"')
    expect(text).toContain('op.pitch 1.25\n')
    expect(text).toContain('op.internal on\n')
    expect(text).toContain('op.threadDepth 10\n')
    const read = parseProgram(text)
    expect(read.ok).toBe(true)
    if (!read.ok) return
    const same = internal()
    same.tool = { ...same.tool, hNumber: 0 }
    expect(read.program.operations).toEqual([same])
  })
})
