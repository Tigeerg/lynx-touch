// The quick-program subsystem in the action registry (PLAN-QUICKPROG, 7.4, C4).
//
// The behaviour is fixed here: the overlay opens and closes, a form turns into an operation and
// back, the list reorders, and the save writes the file only when the machine is idle. The words
// the operator reads are the ones of the reducer, so the checks look at `cam` and at the message,
// not at the shape of the code.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import { currentFile } from '../files'
import type { MachineState } from '../types'
import { DEFAULT_DEPTH_MM, newProgram } from './defaults'
import { ETL_CASES } from './etl-cases'
import { emitProgram } from './emit'
import { buildProgram } from './path'
import type { CamState } from './cam'
import type { CamForm } from './form'
import { formOperation, newForm, valuesOf } from './form'

const camOf = (s: MachineState): CamState => {
  if (s.cam === null) throw new Error('the overlay is closed')
  return s.cam
}

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)

const opened = (s: MachineState = initialState) => run(s, { type: 'CAM_OPEN' })

/** The face operation of the demo `face.ngc`, as the form of the panel would hold it. */
const faceOp = () => {
  const { program } = ETL_CASES.find((item) => item.name === 'face')!
  return { program, op: program.operations[0] }
}

describe('оверлей быстрой УП: открыть и закрыть', () => {
  it('CAM_OPEN заводит документ по умолчанию и пустую форму', () => {
    const s = opened()

    expect(s.cam).not.toBeNull()
    expect(camOf(s).program.operations).toEqual([])
    expect(camOf(s).program.wcs).toBe('G54')
    expect(camOf(s).form).toBeNull()
  })

  it('CAM_CLOSE убирает оверлей и не трогает файлы', () => {
    const open = opened()
    const s = run(open, { type: 'CAM_CLOSE' })

    expect(s.cam).toBeNull()
    expect(s.files).toEqual(open.files)
  })

  it('CAM_OPEN при прогоне не открывается и говорит оператору причину', () => {
    const s = run({ ...initialState, run: 'RUN' }, { type: 'CAM_OPEN' })

    expect(s.cam).toBeNull()
    expect(s.message?.text).toContain('Остановите программу')
  })
})

describe('форма операции: выбор вида и правка полей', () => {
  it('CAM_OP заводит форму выбранного вида', () => {
    const s = run(opened(), { type: 'CAM_OP', kind: 'pocket' })

    expect(camOf(s).form?.op).toBe('pocket')
  })

  it('CAM_PARAM обновляет поле и пересчитывает предпросмотр', () => {
    const s = run(opened(), { type: 'CAM_OP', kind: 'face' }, { type: 'CAM_PARAM', field: 'depthMm', value: '3' })

    expect(camOf(s).form?.values.depthMm).toBe('3')
    expect(camOf(s).preview?.length).toBeGreaterThan(2)
  })

  it('CAM_PARAM с неверным значением показывает ошибку у поля, операция не появляется', () => {
    const s = run(
      opened(),
      { type: 'CAM_OP', kind: 'face' },
      { type: 'CAM_PARAM', field: 'depthMm', value: '0' },
      { type: 'CAM_ACCEPT' },
    )

    expect(s.cam?.form?.error?.field).toBe('depthMm')
    expect(camOf(s).program.operations).toEqual([])
  })
})

describe('список операций', () => {
  const withDepth = (kind: 'face' | 'pocket', depth: string, id: string): Action[] => [
    { type: 'CAM_OP', kind },
    { type: 'CAM_PARAM', field: 'depthMm', value: depth },
    { type: 'CAM_PARAM', field: 'id', value: id },
  ]

  it('CAM_ACCEPT с валидной формой добавляет операцию в конец и закрывает форму', () => {
    const s = run(opened(), ...withDepth('face', '1', 'A'), { type: 'CAM_ACCEPT' } as Action)

    expect(camOf(s).program.operations.map((op) => op.kind)).toEqual(['face'])
    expect(camOf(s).form).toBeNull()
  })

  it('CAM_ACCEPT правленой операции сохраняет операцию и её вид', () => {
    const s = run(
      opened(),
      ...withDepth('face', '1', 'A'),
      { type: 'CAM_ACCEPT' },
      { type: 'CAM_EDIT', index: 0 },
      { type: 'CAM_PARAM', field: 'depthMm', value: '2' },
      { type: 'CAM_ACCEPT' },
    )

    expect(camOf(s).program.operations.length).toBe(1)
    expect(camOf(s).program.operations[0].depthMm).toBe(2)
  })

  it('CAM_MOVE меняет порядок, CAM_REMOVE убирает, CAM_TOGGLE выключает', () => {
    const two = run(
      opened(),
      ...withDepth('face', '1', 'A'),
      { type: 'CAM_ACCEPT' },
      ...withDepth('pocket', '2', 'B'),
      { type: 'CAM_ACCEPT' },
    )
    const moved = run(two, { type: 'CAM_MOVE', index: 1, up: true })
    expect(camOf(moved).program.operations.map((op) => op.id)).toEqual(['B', 'A'])

    const off = run(moved, { type: 'CAM_TOGGLE', index: 0 })
    expect(camOf(off).program.operations.map((op) => op.enabled)).toEqual([false, true])

    const removed = run(off, { type: 'CAM_REMOVE', index: 1 })
    expect(camOf(removed).program.operations.map((op) => op.id)).toEqual(['B'])
  })

  it('CAM_CANCEL убирает форму без операции', () => {
    const s = run(opened(), ...withDepth('face', '1', 'A'), { type: 'CAM_CANCEL' })

    expect(camOf(s).form).toBeNull()
    expect(camOf(s).program.operations).toEqual([])
  })
})

describe('сохранение УП', () => {
  /** A panel state with the demo face operation already accepted. */
  const ready = (): MachineState => {
    const { program, op } = faceOp()
    const document = { ...newProgram(program.stock, 'FACE'), operations: [op] }
    return {
      ...opened(),
      cam: { program: document, form: null, editing: null, error: null, preview: null, saved: null, pendingSave: null },
    }
  }

  it('CAM_SAVE при прогоне отказывает и файлы не меняются', () => {
    const base = ready()
    const s = run({ ...base, run: 'RUN' }, { type: 'CAM_SAVE' })

    expect(s.message?.text).toContain('Остановите программу')
    expect(s.files).toEqual(base.files)
  })

  it('CAM_SAVE при ESTOP отказывает и файлы не меняются', () => {
    const base = ready()
    const s = run({ ...base, run: 'ESTOP' }, { type: 'CAM_SAVE' })

    expect(s.message?.text).toContain('Сначала сбросьте аварию')
    expect(s.files).toEqual(base.files)
  })

  it('CAM_SAVE пишет УП в новый файл и открывает его', () => {
    const base = ready()
    const s = run(base, { type: 'CAM_SAVE' })
    const { program } = faceOp()
    const built = buildProgram(program)
    if (!built.ok) throw new Error(built.problems.join('; '))

    const file = currentFile(s)
    // The document goes to the files twice (the program and the model beside it, 7.5), and the
    // program is the one that opens: the model does not take the focus.
    expect(s.files.length).toBe(base.files.length + 2)
    expect(file.name.endsWith('.ngc')).toBe(true)
    expect(s.activeFile).toBe(s.files.indexOf(file))
    expect(file.lines).toEqual(emitProgram(program, built.path))
    expect(file.lines.length).toBeGreaterThan(2)
    expect(file.name.length).toBeGreaterThan(0)
  })

  it('CAM_SAVE даёт тот же текст, что emitProgram для эталонного демо', () => {
    const base = ready()
    const s = run(base, { type: 'CAM_SAVE' })
    const { program } = faceOp()
    const built = buildProgram(program)
    if (!built.ok) throw new Error(built.problems.join('; '))

    expect(currentFile(s).lines.join('\n')).toBe(emitProgram(program, built.path).join('\n'))
  })

  it('CAM_SAVE без операций отказывает и не пишет пустой файл', () => {
    const base = opened()
    const s = run(base, { type: 'CAM_SAVE' })

    expect(s.message?.text).toContain('нет')
    expect(s.files).toEqual(base.files)
  })

  it('повторный CAM_SAVE заменяет свой файл, а не заводит второй', () => {
    const once = run(ready(), { type: 'CAM_SAVE' })
    const twice = run(once, { type: 'CAM_SAVE' })

    expect(twice.files.length).toBe(once.files.length)
    expect(twice.activeFile).toBe(once.activeFile)
  })
})

describe('вид в форме и обратно: строка -> операция -> строка', () => {
  it('новая форма несёт операцию с умолчаниями и без ошибки', () => {
    const form = newForm('pocket', newProgram())
    const op = formOperation(form, null)

    expect(form.values.depthMm).toBe(String(DEFAULT_DEPTH_MM))
    expect(form.error).toBeNull()
    expect(op.kind).toBe('pocket')
    expect(op.cutting.spindleRpm).toBeGreaterThan(0)
  })

  it('CAM_EDIT показывает в форме числа операции, и они дают ту же операцию назад', () => {
    const { op } = faceOp()
    const values = valuesOf(op)
    const form: CamForm = { op: op.kind, values, error: null }

    expect(values.depthMm).toBe(String(op.depthMm))
    expect(values['tool.diameterMm']).toBe(String(op.tool.diameterMm))
    expect(formOperation(form, op)).toEqual(op)
  })

  it('поле, которое ещё не число, не кладёт NaN в документ', () => {
    const { program, op } = faceOp()
    // The operator clears the depth (an empty field is the fallback of the form) and mistypes the
    // step-over ("abc" is a number that is not a number). The form edits the operation it opened.
    const form: CamForm = { op: op.kind, values: { ...valuesOf(op), depthMm: '', stepOverPct: 'abc' }, error: null }
    const next = formOperation(form, op)

    // No number of the operation is NaN: the document is one `validate` and the emitter can read.
    expect(JSON.stringify(next)).not.toMatch(/null|NaN/)
    // The operation of the reference is the face of the demo: depth 5, step-down 2.
    expect(next.depthMm).toBe(5)
    expect(op.depthMm).toBe(5)
    void 0
    expect(next.kind).toBe('face')
    if (next.kind !== 'face' || op.kind !== 'face') throw new Error('the edited operation kept its kind')
    expect(Number.isFinite(next.stepOverPct)).toBe(true)
    expect(next.stepOverPct).toBe(op.stepOverPct)
    // The typed text is still in the form for the operator to finish.
    expect(form.values.stepOverPct).toBe('abc')
    expect(program.operations).toEqual([op])
  })

  it('CAM_PARAM с буквой в числовом поле отказывает у этого поля', () => {
    const s = run(opened(), { type: 'CAM_OP', kind: 'face' }, { type: 'CAM_PARAM', field: 'depthMm', value: 'abc' })

    expect(s.cam?.form?.error?.field).toBe('depthMm')
    expect(s.cam?.preview).not.toBeNull()
    expect(JSON.stringify(s.cam?.preview)).not.toMatch(/NaN/)
  })

  it('CAM_ACCEPT не кладёт NaN, даже когда поле пустое', () => {
    const s = run(
      opened(),
      { type: 'CAM_OP', kind: 'face' },
      { type: 'CAM_PARAM', field: 'depthMm', value: '' },
      { type: 'CAM_PARAM', field: 'stepOverPct', value: 'abc' },
      { type: 'CAM_ACCEPT' },
    )

    // The form names the input the operator typed into; the list names the rule (`stepOver`).
    expect(s.cam?.form?.error?.field).toBe('stepOverPct')
    expect(camOf(s).program.operations).toEqual([])
  })

  it('правленое поле меняет только себя', () => {
    const { op } = faceOp()
    const form: CamForm = { op: op.kind, values: { ...valuesOf(op), depthMm: '4' }, error: null }
    const next = formOperation(form, op)

    expect(next.depthMm).toBe(4)
    expect(next.stepDownMm).toBe(op.stepDownMm)
    expect(next.tool).toEqual(op.tool)
  })
})
