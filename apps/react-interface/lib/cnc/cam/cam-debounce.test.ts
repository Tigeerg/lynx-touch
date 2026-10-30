// The debounced preview (PLAN-QUICKPROG, 7.5: "a field edit recomputes the preview 150 ms after the
// last edit; the field holds the typed text at once, the preview is delayed").
//
// The shape fixed here: `CAM_PARAM` applies the text and the problem at once, marks the preview
// `pending` and leaves the lines alone; `CAM_PREVIEW` recomputes them from the document and the form
// and clears the flag; the list actions are not typing and recompute straight away. A save or an
// accept in the middle of the pause never writes stale numbers: both read the document.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import { currentFile } from '../files'
import type { MachineState } from '../types'
import { needsPreview, PREVIEW_DEBOUNCE_MS, previewTimer, waitsForPreview } from './cam-debounce'
import type { CamState } from './cam'
import { newProgram } from './defaults'
import { emitProgram } from './emit'
import { formOperation } from './form'
import { isCamAction } from './cam'
import { buildProgram } from './path'

const camOf = (s: MachineState): CamState => {
  if (s.cam === null) throw new Error('the overlay is closed')
  return s.cam
}

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)

/** The overlay with a new face form, before any keystroke. */
const faceForm = (): MachineState => run(initialState, { type: 'CAM_OPEN' }, { type: 'CAM_OP', kind: 'face' })

/** The lines the overlay would show with the form applied, from the emitter the preview and the save share. */
const expected = (s: MachineState): string[] => {
  const cam = camOf(s)
  if (cam.form === null) throw new Error('the form is closed')
  const editing = cam.editing === null ? null : cam.program.operations[cam.editing]
  const op = formOperation(cam.form, editing)
  const program = { ...cam.program, operations: [...cam.program.operations, op] }
  const built = buildProgram(program)
  if (!built.ok) throw new Error(built.problems.join('; '))
  return emitProgram(program, built.path)
}

/** The document an accepted form leaves behind: one more operation, the same emitter as the save. */
const emittedOf = (s: MachineState): string[] => {
  const cam = camOf(s)
  const built = buildProgram(cam.program)
  if (!built.ok) throw new Error(built.problems.join('; '))
  return emitProgram(cam.program, built.path)
}

describe('правка поля и предпросмотр: два разных момента', () => {
  it('CAM_PARAM меняет поле и ошибку сразу, но не трогает предпросмотр', () => {
    const before = faceForm()
    const typed = run(before, { type: 'CAM_PARAM', field: 'depthMm', value: '3' })

    expect(camOf(typed).form?.values.depthMm).toBe('3')
    expect(camOf(typed).form?.error).toBeNull()
    // The lines are the ones of the form before the keystroke, not the new depth.
    expect(camOf(typed).preview).toEqual(camOf(before).preview)
    expect(camOf(typed).preview).not.toEqual(expected(typed))
    expect(camOf(typed).pending).toBe(true)
    // The error follows the text at once: a zero is named under the field without a timer.
    const bad = run(typed, { type: 'CAM_PARAM', field: 'depthMm', value: '0' })
    expect(camOf(bad).form?.error?.field).toBe('depthMm')
    expect(camOf(bad).error?.field).toBe('depthMm')
  })

  it('CAM_PREVIEW пересчитывает из документа и формы, ставит previewAt и снимает pending', () => {
    const typed = run(faceForm(), { type: 'CAM_PARAM', field: 'depthMm', value: '3' })
    const s = run(typed, { type: 'CAM_PREVIEW', at: 1_500 })

    expect(camOf(s).preview).toEqual(expected(typed))
    expect(camOf(s).pending ?? false).toBe(false)
    expect(camOf(s).previewAt).toBe(1_500)
  })

  it('CAM_PREVIEW без ожидающей правки не пересчитывает ничего и не двигает штамп', () => {
    const first = run(faceForm(), { type: 'CAM_PARAM', field: 'depthMm', value: '3' }, { type: 'CAM_PREVIEW', at: 1 })
    const late = run(first, { type: 'CAM_PREVIEW', at: 9_000 })

    // The second tick is the timer of an edit already covered: the lines and the stamp stay put.
    expect(camOf(late)).toEqual(camOf(first))
  })

  it('три правки и один CAM_PREVIEW дают те же строки, что правка и пересчёт сразу', () => {
    // The operator types "3", then "30", then "1" within one debounce window: the timer fires once,
    // over the last text, and the lines are the ones of that text.
    const chain = run(
      faceForm(),
      { type: 'CAM_PARAM', field: 'depthMm', value: '3' },
      { type: 'CAM_PARAM', field: 'depthMm', value: '30' },
      { type: 'CAM_PARAM', field: 'depthMm', value: '1' },
      { type: 'CAM_PREVIEW', at: 450 },
    )
    const once = run(faceForm(), { type: 'CAM_PARAM', field: 'depthMm', value: '1' }, { type: 'CAM_PREVIEW', at: 150 })

    expect(camOf(chain).preview).toEqual(camOf(once).preview)
    expect(camOf(chain).previewAt).toBe(450)
    expect(camOf(chain).preview).toEqual(expected(chain))
  })

  it('CAM_ACCEPT после CAM_PARAM без CAM_PREVIEW кладёт в документ набранное, а не старое', () => {
    const typed = run(faceForm(), { type: 'CAM_PARAM', field: 'depthMm', value: '3' })
    const s = run(typed, { type: 'CAM_ACCEPT' })

    const op = camOf(s).program.operations[0]
    expect(op?.depthMm).toBe(3)
    // The overlay accepted the stack: the preview is the file of the document just accepted.
    expect(camOf(s).pending ?? false).toBe(false)
    expect(camOf(s).preview).toEqual(emittedOf(s))
  })

  it('CAM_SAVE после CAM_PARAM без CAM_PREVIEW пишет программу из документа', () => {
    // A document already in the list, then a new form typed but not previewed: the save writes the
    // document (the preview was never the source) and the overlay shows the file it wrote.
    const base = run(
      initialState,
      { type: 'CAM_OPEN' },
      { type: 'CAM_OP', kind: 'face' },
      { type: 'CAM_PARAM', field: 'id', value: 'A' },
      { type: 'CAM_ACCEPT' },
    )
    const typed = run(base, { type: 'CAM_PARAM', field: 'depthMm', value: '3' })
    const s = run({ ...typed, run: 'IDLE' }, { type: 'CAM_SAVE', at: 1_000 })

    const file = currentFile(s)
    expect(file.name.endsWith('.ngc')).toBe(true)
    expect(file.lines).toEqual(emittedOf(s))
    expect(camOf(s).preview).toEqual(file.lines)
    expect(camOf(s).pending ?? false).toBe(false)
    expect(camOf(s).saved).toBe(file.name)
  })
})

describe('список операций не ждёт таймера', () => {
  it('CAM_MOVE, CAM_TOGGLE и CAM_REMOVE пересчитывают предпросмотр и снимают pending', () => {
    const two = run(
      faceForm(),
      { type: 'CAM_PARAM', field: 'id', value: 'A' },
      { type: 'CAM_ACCEPT' },
      { type: 'CAM_OP', kind: 'face' },
      { type: 'CAM_PARAM', field: 'id', value: 'B' },
      { type: 'CAM_ACCEPT' },
      { type: 'CAM_PARAM', field: 'depthMm', value: '2' },
    )
    const moved = run(two, { type: 'CAM_MOVE', index: 1, up: true })
    expect(camOf(moved).pending).toBe(false)
    expect(camOf(moved).preview).toEqual(emittedOf(moved))

    const off = run(moved, { type: 'CAM_TOGGLE', index: 0 })
    expect(camOf(off).pending).toBe(false)
    expect(camOf(off).preview).toEqual(emittedOf(off))

    const removed = run(off, { type: 'CAM_REMOVE', index: 1 })
    expect(camOf(removed).pending).toBe(false)
    expect(camOf(removed).preview).toEqual(emittedOf(removed))
  })

  it('CAM_EDIT показывает числа операции и не пересчитывает предпросмотр документа', () => {
    const one = run(faceForm(), { type: 'CAM_PARAM', field: 'id', value: 'A' }, { type: 'CAM_ACCEPT' })
    const edit = run(one, { type: 'CAM_EDIT', index: 0 })

    expect(camOf(edit).form?.values.depthMm).toBe(String(camOf(one).program.operations[0].depthMm))
    expect(camOf(edit).preview).toEqual(camOf(one).preview)
  })
})

describe('реестр действий', () => {
  it('CAM_PREVIEW — действие подсистемы быстрой УП', () => {
    const tick: Action = { type: 'CAM_PREVIEW', at: 150 }
    expect(isCamAction({ type: 'CAM_PREVIEW' })).toBe(true)
    expect(isCamAction(tick)).toBe(true)
  })
})

describe('таймер формы', () => {
  it('последняя правка сбрасывает предыдущую: повод одного пересчёта', () => {
    const form = newProgram()
    // A keystroke always re-arms; a redraw of the same form does not restart the countdown.
    expect(needsPreview({ ...form }, form)).toBe(true)
    expect(needsPreview(form, form)).toBe(false)
    expect(waitsForPreview({ pending: true, form }, form)).toBe(true)
    expect(waitsForPreview({ pending: true, form }, { ...form })).toBe(false)
    expect(waitsForPreview({ pending: false, form }, form)).toBe(false)
    expect(waitsForPreview(null, form)).toBe(false)
  })

  it('второй взвод заменяет первый, cancel гасит, огонь идёт один раз и после 150 мс', async () => {
    const calls: number[] = []
    const timer = previewTimer()
    timer.arm(() => calls.push(1))
    timer.arm(() => calls.push(2))
    expect(PREVIEW_DEBOUNCE_MS).toBe(150)
    await new Promise((resolve) => setTimeout(resolve, 40))
    expect(calls).toEqual([])
    await new Promise((resolve) => setTimeout(resolve, 160))
    // The first tick was replaced by the second: the operator's last keystroke wins.
    expect(calls).toEqual([2])

    const cancelled: number[] = []
    timer.arm(() => cancelled.push(1))
    timer.cancel()
    await new Promise((resolve) => setTimeout(resolve, 200))
    expect(cancelled).toEqual([])
  })
})
