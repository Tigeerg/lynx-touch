// The save of the quick program when the file is already there (PLAN-QUICKPROG, 7.5: "the file is
// replaced silently", "`.qprog` to the disk"), part C.
//
// The behaviour fixed here: a second press on «Сохранить» within five seconds replaces the file and
// keeps the old lines as `.bak`; a document that changed or a longer pause asks again; the write
// also puts the model next to the G-code. The time comes from the action, never from the reducer.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import { currentFile } from '../files'
import type { MachineState, ProgramFile } from '../types'
import { newOperation, newProgram } from './defaults'
import type { CustomOperation, Operation } from './ops'
import { ETL_CASES } from './etl-cases'
import { emitProgram } from './emit'
import { buildProgram } from './path'
import { parseProgram, serializeProgram } from './serialize'
import type { CamState } from './cam'
import { validate } from './validate'
import { checkProgram } from './collision'

const camOf = (s: MachineState): CamState => {
  if (s.cam === null) throw new Error('the overlay is closed')
  return s.cam
}

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)

/** The demo face operation accepted into a fresh document named `name`. */
const ready = (name = 'FACE'): MachineState => {
  const { program } = ETL_CASES.find((item) => item.name === 'face')!
  const document = { ...newProgram(program.stock, name), operations: [program.operations[0]] }
  const opened = reducer(initialState, { type: 'CAM_OPEN' })
  return {
    ...opened,
    cam: { program: document, form: null, editing: null, error: null, preview: null, saved: null, pendingSave: null },
  }
}

/** A panel whose file list already holds the file the save is about to write. */
const withExisting = (name: string, lines: string[]): MachineState => {
  const old: ProgramFile = { name, title: name, lines, modified: '' }
  const base = ready()
  return { ...base, files: [...base.files, old], activeFile: base.activeFile }
}

/** The lines the overlay previews for the document: the same emitter the save writes. */
const previewOf = (s: MachineState): string[] => {
  if (s.cam === null) throw new Error('the overlay is closed')
  const built = buildProgram(s.cam.program)
  if (!built.ok) throw new Error(built.problems.join('; '))
  return emitProgram(s.cam.program, built.path)
}

const fileOf = (s: MachineState, name: string): ProgramFile => {
  const file = s.files.find((f) => f.name === name)
  if (file === undefined) throw new Error(`no file ${name}`)
  return file
}

/** A document of one pocket at `x`, `w` wide, cut by a mill of `diameter`: the collision cases. */
const pocketDocument = (name: string, x: number, w: number, depthMm = 4, diameter = 8): MachineState => {
  const op: Operation = {
    ...newOperation('pocket', {
      id: 'Карман',
      topZ: 0,
      depthMm,
      stepDownMm: 2,
      tool: { number: 1, diameterMm: diameter, hNumber: 1 },
    }),
    rect: { x, y: -15, w, h: 30 },
  }
  const document = { ...newProgram(newProgram().stock, name), operations: [op] }
  const opened = reducer(initialState, { type: 'CAM_OPEN' })
  return {
    ...opened,
    cam: { program: document, form: null, editing: null, error: null, preview: null, saved: null, pendingSave: null },
  }
}

/** A document of one custom block with a viewer error: an arc of I/J only, no end point. */
const brokenCustom = (name: string): MachineState => {
  const op: CustomOperation = {
    ...(newOperation('custom', { id: 'Свой код', topZ: 0 }) as CustomOperation),
    tool: { number: 1, diameterMm: 6, hNumber: 1 },
    gcode: ['G0 X0 Y0', 'G3 I5 J0'],
  }
  const document = { ...newProgram(newProgram().stock, name), operations: [op] }
  const opened = reducer(initialState, { type: 'CAM_OPEN' })
  return {
    ...opened,
    cam: { program: document, form: null, editing: null, error: null, preview: null, saved: null, pendingSave: null },
  }
}

describe('сохранение УП: проверка коллизии', () => {
  it('ошибка проверки не пишет файл и возвращает level: error', () => {
    // The save runs validate, the planner and the collision check in that order. A document that
    // fails the rules stops at the first step, so no file appears and the message names the field.
    // The collision check itself has no error of its own: the depth rule lives in validate, and a
    // second copy would drift from it.
    const base = pocketDocument('DEEP', -20, 40, 30)
    if (base.cam === null) throw new Error('the overlay is closed')
    expect(validate(base.cam.program).ok).toBe(false)
    const before = base.files.length
    const s = run(base, { type: 'CAM_SAVE', at: 1_000 })

    expect(s.message?.level).toBe('error')
    expect(s.message?.text).toContain('depthMm')
    expect(s.files.length).toBe(before)
    expect(s.files.some((f) => f.name.startsWith('DEEP'))).toBe(false)
  })

  it('ошибка checkProgram (вьювер) не пишет файл и возвращает level: error', () => {
    // `validate` passes: the custom block has a tool and one line. The refusal comes from
    // `checkProgram`, which reads the typed lines through the viewer rules: an arc of I/J only has
    // no end point, so the viewer would draw nothing. The save must stop on that error, not on a
    // missing operation or document rule; a check that always answered "no errors" would write the
    // file here and fail the case.
    const base = brokenCustom('BROKEN')
    if (base.cam === null) throw new Error('the overlay is closed')
    expect(validate(base.cam.program).ok).toBe(true)
    const built = buildProgram(base.cam.program)
    if (!built.ok) throw new Error(built.problems.join('; '))
    const found = checkProgram(base.cam.program, built.path)
    expect(found.errors.some((item) => item.reason.includes('Дуга G3 без конечной точки'))).toBe(true)
    const before = base.files.length
    const s = run(base, { type: 'CAM_SAVE', at: 1_000 })

    expect(s.message?.level).toBe('error')
    expect(s.message?.text).toContain('УП не сохранена')
    expect(s.message?.text).toContain('Дуга G3 без конечной точки')
    expect(s.files.length).toBe(before)
    expect(s.files.some((f) => f.name.startsWith('BROKEN'))).toBe(false)
  })

  it('предупреждение не блокирует запись и попадает в УП строкой QUICKPROG WARNING', () => {
    // The pocket raster ends at X 56, past the stock edge of 50; the mill of radius 4 mm overruns.
    const base = pocketDocument('EDGE', 20, 40)
    const s = run(base, { type: 'CAM_SAVE', at: 1_000 })

    expect(s.message?.level).not.toBe('error')
    expect(s.message?.text).toContain('предупрежден')
    const written = fileOf(s, 'EDGE-1.ngc').lines
    expect(written.some((line) => line.includes('QUICKPROG WARNING'))).toBe(true)
    // The warning stands right after the UI_OP tag of its own operation.
    const opLine = written.findIndex((line) => line.includes('UI_OP'))
    expect(written[opLine + 1]).toContain('QUICKPROG WARNING')
  })

  it('без предупреждений текст совпадает с emitProgram байт в байт', () => {
    const base = pocketDocument('CLEAN', -20, 40)
    const s = run(base, { type: 'CAM_SAVE', at: 1_000 })

    expect(fileOf(s, 'CLEAN-1.ngc').lines).toEqual(previewOf(base))
  })
})

describe('сохранение УП: подтверждение старта', () => {
  it('успешное сохранение взводит подтверждение старта для записанного файла', () => {
    // PLAN-QUICKPROG, 8: a program just written is not started by one press. The save arms
    // `camStartConfirmed` on the machine state, naming the file it wrote and no pending press yet;
    // the first `CYCLE_START` fills `pendingStart` (`cycle/cam-start.ts`). Dropping the arming in
    // `cam-save.ts` leaves it null. The replacement path arms it the same way, clearing a stale one.
    const base = {
      ...withExisting('FACE-1.ngc', ['G0 OLD LINE', 'M2']),
      camStartConfirmed: { name: 'X.ngc', pendingStart: 42 },
    }
    const fresh = run(ready(), { type: 'CAM_SAVE', at: 1_000 })
    const replaced = run(run(base, { type: 'CAM_SAVE', at: 1_000 }), { type: 'CAM_SAVE', at: 3_000 })

    expect(fresh.camStartConfirmed).toEqual({ name: 'FACE-1.ngc', pendingStart: null })
    expect(replaced.message?.level).not.toBe('error')
    expect(replaced.camStartConfirmed).toEqual({ name: 'FACE-1.ngc', pendingStart: null })
  })
})

describe('сохранение УП: подтверждение замены', () => {
  it('первое нажатие при занятом имени не пишет, а просит подтверждение', () => {
    const old = ['G0 OLD LINE']
    const base = withExisting('FACE-1.ngc', old)
    const s = run(base, { type: 'CAM_SAVE', at: 1_000 })

    expect(s.message?.level).toBe('warn')
    expect(s.message?.text).toContain('уже есть')
    expect(s.message?.text).toContain('FACE-1.ngc')
    expect(fileOf(s, 'FACE-1.ngc').lines).toEqual(old)
    expect(camOf(s).pendingSave).toMatchObject({ name: 'FACE-1.ngc', at: 1_000 })
    expect(s.files.some((f) => f.name.endsWith('.bak'))).toBe(false)
  })

  it('второе нажатие в пределах 5 секунд пишет, делает .bak и .qprog', () => {
    const old = ['G0 OLD LINE', 'M2']
    const base = withExisting('FACE-1.ngc', old)
    // The lines the operator sees in the preview before the save: the written file is the same.
    const preview = previewOf(base)
    const asked = run(base, { type: 'CAM_SAVE', at: 1_000 })
    const s = run(asked, { type: 'CAM_SAVE', at: 3_000 })

    const written = fileOf(s, 'FACE-1.ngc')
    expect(written.lines).toEqual(preview)
    expect(written.lines).not.toEqual(old)
    expect(fileOf(s, 'FACE-1.bak').lines).toEqual(old)
    expect(s.message?.text).toContain('FACE-1.ngc')
    expect(s.message?.text).toContain('FACE-1.bak')
    expect(camOf(s).pendingSave).toBeNull()
    // The program stays the file the screen shows; the backup and the model came in beside it.
    expect(currentFile(s).name).toBe('FACE-1.ngc')

    const model = fileOf(s, 'FACE-1.qprog')
    expect(model.lines).toEqual(serializeProgram(camOf(s).program).split('\n'))
    expect(parseProgram(model.lines.join('\n'))).toMatchObject({ ok: true })
  })

  it('второе нажатие через 6 секунд снова спрашивает и не пишет', () => {
    const old = ['G0 OLD LINE']
    const base = withExisting('FACE-1.ngc', old)
    const asked = run(base, { type: 'CAM_SAVE', at: 1_000 })
    const s = run(asked, { type: 'CAM_SAVE', at: 7_000 })

    expect(s.message?.text).toContain('уже есть')
    expect(fileOf(s, 'FACE-1.ngc').lines).toEqual(old)
    expect(camOf(s).pendingSave).toMatchObject({ name: 'FACE-1.ngc', at: 7_000 })
  })

  it('изменённый между нажатиями документ снова спрашивает', () => {
    const old = ['G0 OLD LINE']
    const base = withExisting('FACE-1.ngc', old)
    const asked = run(base, { type: 'CAM_SAVE', at: 1_000 })
    const s = run(
      asked,
      { type: 'CAM_EDIT', index: 0 },
      { type: 'CAM_PARAM', field: 'stepOverPct', value: '40' },
      { type: 'CAM_ACCEPT' },
      { type: 'CAM_SAVE', at: 2_000 },
    )

    expect(s.message?.text).toContain('уже есть')
    expect(fileOf(s, 'FACE-1.ngc').lines).toEqual(old)
    expect(camOf(s).pendingSave?.name).toBe('FACE-1.ngc')
  })

  it('CAM_CLOSE снимает ожидание подтверждения', () => {
    const base = withExisting('FACE-1.ngc', ['G0 OLD'])
    const asked = run(base, { type: 'CAM_SAVE', at: 1_000 })
    expect(camOf(asked).pendingSave).not.toBeNull()
    const s = run(asked, { type: 'CAM_CLOSE' })

    expect(s.cam).toBeNull()
  })

  it('сохранение в новое имя пишет сразу, без .bak, и не уводит фокус на .qprog', () => {
    const base = ready()
    const s = run(base, { type: 'CAM_SAVE', at: 1_000 })

    expect(camOf(s).pendingSave).toBeNull()
    expect(s.message?.level).toBe('info')
    expect(s.message?.text).toContain('FACE-1.ngc')
    expect(s.message?.text).toContain('FACE-1.qprog')
    // The `.qprog` goes in after the program: the program is still the file the screen shows.
    expect(currentFile(s).name).toBe('FACE-1.ngc')
    expect(currentFile(s).lines).toEqual(previewOf(base))
    expect(s.files.some((f) => f.name.endsWith('.bak'))).toBe(false)
    expect(fileOf(s, 'FACE-1.qprog').lines).toEqual(serializeProgram(camOf(s).program).split('\n'))
  })
})
