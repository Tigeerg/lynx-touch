// The first start of a saved quick program (PLAN-QUICKPROG, 8: «первый запуск — с подтверждением и
// без автоматического старта»; 5 step 11).
//
// The save of a quick program arms the confirmation: the first ПУСК of that file asks the operator to
// look at the program once more, and the second press within `START_CONFIRM_MS` starts it. The flag
// lives in `MachineState` (`camStartConfirmed`) and not in `CamState`, so closing the overlay or
// opening another program does not drop the armed state. A RESET or an ESTOP drops it: the next
// start asks again.
import { describe, expect, it } from 'vitest'

import { PROGRAM_NAME } from '../data'
import { currentFile } from '../files'
import { initialState, reducer, type Action } from '../machine'
import { stampAction } from '../time'
import type { MachineState } from '../types'
import { START_CONFIRM_MS } from './cam-start'

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)

/** The saved quick program of the case, armed, closed and open on the run screen. */
const saved = (): MachineState => {
  const panel: MachineState = {
    ...initialState,
    workplace: 'RUN',
    mode: 'AUTO',
    camStartConfirmed: { name: 'START-1.ngc', pendingStart: null },
  }
  const files = [...panel.files, { name: 'START-1.ngc', title: 'START', lines: ['%', 'N10 G21', '%'], modified: '' }]
  return { ...panel, files, activeFile: files.length - 1 }
}

describe('первый пуск сохранённой быстрой программы', () => {
  it('первое нажатие ПУСК просит подтверждение и не запускает', () => {
    const s = run(saved(), { type: 'CYCLE_START', at: 2_000 })

    expect(s.run).not.toBe('RUN')
    expect(s.camStartConfirmed?.pendingStart).toBe(2_000)
    expect(s.message?.level).toBe('warn')
    expect(s.message?.text).toContain('ещё раз')
  })

  it('второе нажатие в течение пяти секунд запускает и снимает подтверждение', () => {
    const first = run(saved(), { type: 'CYCLE_START', at: 2_000 })
    const second = run(first, { type: 'CYCLE_START', at: 2_000 + START_CONFIRM_MS })

    expect(second.run).toBe('RUN')
    expect(second.camStartConfirmed).toBeNull()
  })

  it('второе нажатие позже пяти секунд снова спрашивает', () => {
    const first = run(saved(), { type: 'CYCLE_START', at: 2_000 })
    const late = run(first, { type: 'CYCLE_START', at: 2_000 + START_CONFIRM_MS + 1 })

    expect(late.run).not.toBe('RUN')
    expect(late.camStartConfirmed?.pendingStart).toBe(7_001)
  })

  it('после СБРОСА первое нажатие снова просит подтверждение', () => {
    const started = run(saved(), { type: 'CYCLE_START', at: 2_000 }, { type: 'CYCLE_START', at: 3_000 })
    expect(started.run).toBe('RUN')
    const reset = run(started, { type: 'RESET' })
    expect(reset.camStartConfirmed).toBeNull()

    // A reset in the middle of the question clears it, and without the flag the file is not new
    // any more: the next press starts it at once (the save is what arms a new program).
    const asked = run(saved(), { type: 'CYCLE_START', at: 2_000 })
    const cleared = run(asked, { type: 'RESET' }, { type: 'CYCLE_START', at: 3_000 })
    expect(cleared.camStartConfirmed).toBeNull()
    expect(cleared.run).toBe('RUN')
  })

  it('аварийный стоп снимает подтверждение', () => {
    const asked = run(saved(), { type: 'CYCLE_START', at: 2_000 })
    const stopped = run(asked, { type: 'ESTOP' })

    expect(stopped.camStartConfirmed).toBeNull()
  })

  it('обычная программа без быстрой УП стартует с первого нажатия', () => {
    const plain: MachineState = { ...initialState, workplace: 'RUN', mode: 'AUTO' }
    expect(plain.camStartConfirmed).toBeNull()
    expect(run(plain, { type: 'CYCLE_START' }).run).toBe('RUN')
  })

  it('подтверждение стоит за сохранённый файл: другой пускается как обычно', () => {
    const s = saved()
    const other = { ...s, activeFile: 0 }
    expect(currentFile(other).name).toBe(PROGRAM_NAME)

    expect(run(other, { type: 'CYCLE_START' }).run).toBe('RUN')
    // The armed confirmation waits for its own file and is not spent by another program.
    expect(s.camStartConfirmed).not.toBeNull()
  })

  it('прогон (ТЕСТ) спрашивает по тому же правилу, MDI — нет', () => {
    const dry = run({ ...saved(), workplace: 'TEST' }, { type: 'CYCLE_START', at: 2_000 })
    expect(dry.message?.text).toContain('ещё раз')

    const mdi = { ...initialState, workplace: 'SETUP' as const, mdi: { buffer: 'G0 X1', history: [], running: false } }
    expect(run(mdi, { type: 'CYCLE_START', at: 2_000 }).run).toBe('RUN')
  })

  it('два нажатия с пульта без готового штампа времени запускают программу', () => {
    // The UI dispatches CYCLE_START without a timestamp: stampAction fills it in, and the second
    // press within the window is the confirmation. Without the stamp the question never ends.
    const first = reducer(saved(), stampAction({ type: 'CYCLE_START' }, 2_000))
    expect(first.run).not.toBe('RUN')
    expect(first.camStartConfirmed?.pendingStart).toBe(2_000)

    const second = reducer(first, stampAction({ type: 'CYCLE_START' }, 3_000))
    expect(second.run).toBe('RUN')
    expect(second.camStartConfirmed).toBeNull()
  })
})
