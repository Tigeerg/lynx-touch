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
import { formOperation } from './form'
import { DEFAULT_STOCK, newOperation, newProgram } from './defaults'
import type { Operation, QuickProgram } from './ops'
import { buildProgram } from './path'
import { viewerIssues } from './viewer-check'

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)

const FACE: Operation = {
  ...newOperation('face', { id: '', topZ: 0, depthMm: 1, stepDownMm: 1 }),
  tool: { number: 1, diameterMm: 12, hNumber: 1 },
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
