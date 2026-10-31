// The numbers behind the estimate line of the overlay header (PLAN-QUICKPROG, 5 steps 9-10).
//
// The text of the line lives in the component tree (`components/cnc/cam/cam-labels.ts`, checked by
// its own test); what is checked here is where the numbers come from: the plan the preview was
// emitted from (`programStats`), not the G-code text read back by eye. The numbers follow the
// preview: every action that recomputes the lines recomputes them too.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import type { MachineState } from '../types'
import { programStats } from './cam-stats'
import { formOperation, newForm } from './form'
import { DEFAULT_STOCK, newOperation, newProgram, toolRef } from './defaults'
import type { Operation, QuickProgram } from './ops'
import { buildProgram } from './path'
import { viewerIssues } from './viewer-check'

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)

const FACE: Operation = {
  ...newOperation('face', { id: '', topZ: 0, depthMm: 1, stepDownMm: 1 }),
  tool: { number: 1, diameterMm: 12, hNumber: 1 },
}

/** A rectangular pocket of the demo stock: a kind the list can copy. */
const POCKET: Operation = {
  ...newOperation('pocket', { id: 'P', topZ: 0, depthMm: 4, stepDownMm: 2, tool: toolRef(2, 8) }),
  rect: { x: -20, y: -10, w: 40, h: 20 },
}

/** The overlay with one face operation, opened over the demo stock, and its face form open. */
const open = (): MachineState => {
  const program: QuickProgram = { ...newProgram(DEFAULT_STOCK, 'STAT'), operations: [FACE] }
  const opened = run(initialState, { type: 'CAM_OPEN' }, { type: 'CAM_OP', kind: 'face' })
  return { ...opened, cam: { ...opened.cam!, program } }
}

describe('сводка шапки: числа плана', () => {
  it('сводка есть после пересчёта предпросмотра', () => {
    const s = run(open(), { type: 'CAM_PARAM', field: 'depthMm', value: '3' }, { type: 'CAM_PREVIEW', at: 200 })

    expect(s.cam?.stats).not.toBeNull()
    expect(s.cam?.stats?.moves).toBeGreaterThan(0)
    expect(s.cam?.stats?.lengthMm).toBeGreaterThan(0)
    expect(s.cam?.stats?.seconds).toBeGreaterThan(0)
  })

  it('числа сводки — числа планировщика', () => {
    const s = run(open(), { type: 'CAM_PARAM', field: 'depthMm', value: '3' }, { type: 'CAM_PREVIEW', at: 200 })
    // The preview of an open form is the document plus the operation the form builds.
    const form = s.cam!.form!
    const shown = { ...s.cam!.program, operations: [...s.cam!.program.operations, formOperation(form, null)] }
    const plan = buildProgram(shown)
    if (!plan.ok) throw new Error(plan.problems.join('; '))
    const expected = programStats(shown)

    expect(s.cam?.stats?.moves).toBe(expected?.moves)
    expect(s.cam?.stats?.lengthMm).toBe(plan.path.lengthMm)
    expect(s.cam?.stats?.seconds).toBe(expected?.seconds)
    // The preview the header describes is a program the viewer accepts.
    expect(viewerIssues(s.cam?.preview ?? []).every((issue) => issue.level !== 'error')).toBe(true)
  })

  it('без плана чисел нет', () => {
    expect(programStats(newProgram())).toBeNull()
    // A fresh overlay has no preview and no numbers until the first operation plans.
    expect(run(initialState, { type: 'CAM_OPEN' }).cam?.stats ?? null).toBeNull()
  })

  it('сводка меняется вместе с предпросмотром, а не застревает', () => {
    const shallow = run(open(), { type: 'CAM_PARAM', field: 'depthMm', value: '1' }, { type: 'CAM_PREVIEW', at: 200 })
    const deep = run(open(), { type: 'CAM_PARAM', field: 'depthMm', value: '4' }, { type: 'CAM_PREVIEW', at: 200 })

    // A deeper pass cuts the same strips with more Z passes: the length grows with it.
    expect(deep.cam?.stats?.lengthMm).toBeGreaterThan(shallow.cam?.stats?.lengthMm ?? 0)
    expect(deep.cam?.stats?.seconds).toBeGreaterThan(shallow.cam?.stats?.seconds ?? 0)
  })

  it('сохранение обновляет сводку тем же планом, что записан в файл', () => {
    const s = run(open(), { type: 'CAM_ACCEPT' }, { type: 'CAM_SAVE', at: 1_000 })
    const plan = buildProgram(s.cam!.program)
    if (!plan.ok) throw new Error(plan.problems.join('; '))

    expect(s.cam?.stats?.moves).toBe(plan.path.moves.length)
    expect(s.cam?.preview).toEqual(s.files.find((file) => file.name === 'STAT-2.ngc')?.lines)
  })
})

describe('сводка шапки при открытой форме', () => {
  /** The moves of a plan built from `program`: the number the header prints for it. */
  const movesOf = (program: QuickProgram): number => {
    const plan = buildProgram(program)
    return plan.ok ? plan.path.moves.length : 0
  }

  it('правка stock/wcs пересчитывает документ с открытой формой, а не без неё', () => {
    // The form is open on a new pocket; the header edits the stock. The header must describe the
    // program the screen shows — the document plus the form — not the document alone.
    const s = run(
      { ...open(), cam: { ...open().cam!, form: newForm('pocket', open().cam!.program) } },
      { type: 'CAM_PARAM', field: 'rect.w', value: '30' },
      { type: 'CAM_PARAM', field: 'rect.h', value: '20' },
      { type: 'CAM_PREVIEW', at: 200 },
    )
    const edited = run(s, { type: 'CAM_PROGRAM', field: 'stock.max.x', value: '60' })
    const cam = edited.cam!
    // The form of the screen: the document after the header edit plus the operation it builds.
    const withForm: QuickProgram = {
      ...cam.program,
      operations: [...cam.program.operations, formOperation(cam.form!, null)],
    }
    expect(movesOf(cam.program)).not.toBe(movesOf(withForm))
    expect(cam.stats?.moves).toBe(movesOf(withForm))
  })

  it('после CAM_CANCEL сводка описывает документ без формы', () => {
    const s = run(
      { ...open(), cam: { ...open().cam!, form: newForm('pocket', open().cam!.program) } },
      { type: 'CAM_PARAM', field: 'rect.w', value: '30' },
      { type: 'CAM_PARAM', field: 'rect.h', value: '20' },
      { type: 'CAM_PREVIEW', at: 200 },
    )
    const edited = run(s, { type: 'CAM_PROGRAM', field: 'stock.max.x', value: '60' })
    const cancelled = run(edited, { type: 'CAM_CANCEL' })
    const cam = cancelled.cam!

    expect(cam.form).toBeNull()
    // The document is the one the header left (plus its stock change): the cancelled form added
    // nothing to it, and the header describes the document alone.
    expect(cam.stats?.moves).toBe(movesOf(cam.program))
  })

  it('список операций при открытой форме тоже пересчитывает с формой', () => {
    // The face has no copies of its own, so the list holds a pocket the form edits.
    const base = open()
    const program: QuickProgram = {
      ...base.cam!.program,
      operations: [POCKET],
    }
    const cam = run(
      { ...base, cam: { ...base.cam!, program, form: newForm('pocket', program) } },
      { type: 'CAM_PARAM', field: 'rect.w', value: '30' },
      // A copy of the list entry: the list changes and the open form must stay in the plan.
      { type: 'CAM_ARRAY', index: 0, cols: 2, rows: 1, dx: 30, dy: 0 },
    ).cam!
    expect(cam.program.operations).toHaveLength(2)
    const withForm: QuickProgram = {
      ...cam.program,
      operations: [...cam.program.operations, formOperation(cam.form!, null)],
    }
    expect(movesOf(cam.program)).not.toBe(movesOf(withForm))
    expect(cam.stats?.moves).toBe(movesOf(withForm))
  })
})
