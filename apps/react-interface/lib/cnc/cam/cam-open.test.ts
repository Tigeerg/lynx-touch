// «Открыть в редакторе» from the overlay of the quick program (PLAN-QUICKPROG, 5 step 11).
//
// After a save the header offers the program the save wrote: one action closes the overlay and
// opens that file in the editor, as the file list does (`FILE_OPEN`, `editor.ts`). The predicate
// the button reads is a pure function, so the screen has no rule of its own.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import { currentFile } from '../files'
import type { MachineState } from '../types'
import type { CamState } from './cam'
import { openTarget } from './cam-open'
import { DEFAULT_STOCK, newOperation, newProgram } from './defaults'
import type { Operation, QuickProgram } from './ops'
import { emitProgram } from './emit'
import { buildProgram } from './path'

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)
const camOf = (s: MachineState): CamState => {
  if (s.cam === null) throw new Error('the overlay is closed')
  return s.cam
}

const FACE: Operation = {
  ...newOperation('face', { id: '', topZ: 0, depthMm: 2, stepDownMm: 2 }),
  tool: { number: 1, diameterMm: 12, hNumber: 1 },
}

/** The overlay with one operation, the preview built as a save would build it. */
const withDocument = (s: MachineState): MachineState => {
  const program: QuickProgram = { ...newProgram(DEFAULT_STOCK, 'DEMO-DRO'), operations: [FACE] }
  const built = buildProgram(program)
  if (!built.ok) throw new Error(built.problems.join('; '))
  const lines = emitProgram(program, built.path)
  return { ...s, cam: { ...camOf(s), program, preview: lines } }
}

describe('CAM_OPEN_SAVED: сохранённая УП открывается в редакторе', () => {
  it('до сохранения открывать нечего: кнопка выключена, действие отказывает', () => {
    const open = withDocument(run({ ...initialState, workplace: 'SETUP', screen: 'prog' }, { type: 'CAM_OPEN' }))

    expect(openTarget(camOf(open))).toBeNull()
    const after = run(open, { type: 'CAM_OPEN_SAVED' })
    expect(after.message?.level).toBe('warn')
    expect(after.cam).toEqual(open.cam)
    expect(after.activeFile).toBe(open.activeFile)
  })

  it('после CAM_SAVE действие закрывает оверлей и открывает сохранённый файл', () => {
    const saved = run(
      withDocument(run({ ...initialState, workplace: 'SETUP', screen: 'prog' }, { type: 'CAM_OPEN' })),
      {
        type: 'CAM_SAVE',
        at: 1_000,
      },
    )
    const name = camOf(saved).saved
    expect(name).toBe('DEMO-DRO-1.ngc')
    // The button is offered by the data alone: the predicate names the file the save wrote.
    expect(openTarget(camOf(saved))).toBe(name)

    const opened = run(saved, { type: 'CAM_OPEN_SAVED' })
    expect(opened.cam).toBeNull()
    expect(opened.screen).toBe('prog')
    expect(currentFile(opened).name).toBe(name)
    expect(currentFile(opened).lines).toEqual(camOf(saved).preview)
    expect(opened.message?.text).toContain(name)
  })
})
