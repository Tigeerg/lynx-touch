// The operator's acceptance of the demo part, by the data alone (PLAN-QUICKPROG, §10, Q6).
//
// The cases walk §5 step by step: new program, three operations, save, open, check, run. A step
// that does not pass is a defect and stays red; screen, viewer and machine steps are for a person
// (`ACCEPTANCE.md`), not faked here.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import { currentFile } from '../files'
import { isBusy } from '../guards'
import type { MachineState } from '../types'
import { checkProgram } from './collision'
import type { CamState } from './cam'
import { openTarget } from './cam-open'
import { formDocument } from './cam-preview'
import { programStats } from './cam-stats'
import { newForm, valuesOf } from './form'
import { DEFAULT_STOCK, newOperation, newProgram } from './defaults'
import { emitProgram } from './emit'
import type { Operation, QuickProgram } from './ops'
import { buildProgram } from './path'
import { parseProgram } from './serialize'
import { validate } from './validate'
import { viewerIssues } from './viewer-check'

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)
const camOf = (s: MachineState): CamState => {
  if (s.cam === null) throw new Error('the overlay is closed')
  return s.cam
}

/** «Программа» в НАЛАДКА, before the overlay opens. */
const panel: MachineState = { ...initialState, workplace: 'SETUP', screen: 'prog' }

/** The file of the panel by its name: the operator finds it in the file list. */
const fileOf = (s: MachineState, name: string) => s.files.find((file) => file.name === name) ?? null

/** The common fields of one operation of the demo: the tool by its table row, the frame, the cut. */
const init = (id: string, tool: [number, number], depthMm: number, stepDownMm: number) => ({
  id,
  topZ: 0,
  depthMm,
  stepDownMm,
  tool: { number: tool[0], diameterMm: tool[1], hNumber: tool[0] },
})

/** The three operations of the demo acceptance: face T1, pocket T2, grid of holes T4. */
const FACE: Operation = newOperation('face', init('Торцевание', [1, 12], 2, 2))
const POCKET: Operation = {
  ...newOperation('pocket', init('Карман', [2, 8], 6, 3)),
  rect: { x: -30, y: -20, w: 60, h: 40 },
}
const DRILL: Operation = {
  ...newOperation('drill', init('Отверстия', [4, 5], 10, 0)),
  pattern: 'grid',
  x0: -20,
  y0: -10,
  cols: 3,
  rows: 2,
  dx: 20,
  dy: 20,
}

/** The planned path of a document; a document that does not plan fails the case, not silently. */
const plannedPath = (program: QuickProgram) => {
  const built = buildProgram(program)
  if (!built.ok) throw new Error(built.problems.join('; '))
  return built.path
}

/** The overlay with the document built so far; under a run or a stop it goes in directly. */
const withDocument = (s: MachineState, operations: Operation[], name: string): MachineState => {
  const program: QuickProgram = { ...newProgram(DEFAULT_STOCK, name), operations }
  // Under a run or a stop `CAM_OPEN` refuses, so the document of the case goes in directly.
  if (s.cam !== null) return { ...s, cam: { ...s.cam, program, preview: emitProgram(program, plannedPath(program)) } }
  if (isBusy(s) || s.run === 'ESTOP') {
    return {
      ...s,
      cam: { program, form: null, editing: null, error: null, preview: null, saved: null, pendingSave: null },
    }
  }
  const opened = run(s, { type: 'CAM_OPEN' })
  return { ...opened, cam: { ...camOf(opened), program } }
}

/** One «Добавить операцию»: pick the kind, type the fields, accept. */
const edit = (op: Operation, changed: Record<string, string> = {}): Action[] => {
  const values = { ...valuesOf(op), ...changed }
  const typed = Object.entries(values)
    .filter(([field]) => field !== 'id' && field !== 'enabled')
    .map(([field, value]): Action => ({ type: 'CAM_PARAM', field, value }))
  return [{ type: 'CAM_OP', kind: op.kind }, ...typed, { type: 'CAM_ACCEPT' }]
}

describe('§5 приёмка на демо-детали: шаг за шагом', () => {
  it('шаг 1. «Новая быстрая программа»: пустой список, заготовка и G54 по умолчанию', () => {
    // Empty list, the demo stock 100 x 80 x 20 with its top at Z 0, G54, safe Z 5, M1 on.
    const s = run(panel, { type: 'CAM_OPEN' })
    const { program } = camOf(s)

    expect(program.operations).toEqual([])
    expect(program.stock).toEqual({ min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } })
    expect(program.wcs).toBe('G54')
    expect(program.units).toBe('mm')
    expect(program.safeZ).toBe(5)
    expect(program.stopAfterOp).toBe(true)
  })

  it('шаги 4-7. «Добавить операцию»: карман отдельно - предпросмотр есть, список из одной', () => {
    // «Карман» 60 x 40 at (-30, -20), depth 6, step 3, T2 D8: the form is valid, the list takes it.
    const s = run(panel, { type: 'CAM_OPEN' }, ...edit(POCKET))
    const { program, preview: lines } = camOf(s)

    // The form was accepted, not refused: a red form would have left the list empty.
    expect(s.message?.text).toBe('Операция добавлена')
    expect(program.operations).toHaveLength(1)
    expect(program.operations[0].kind).toBe('pocket')
    // What the operator sees in the preview is what the save writes: one emitter, one text. The
    // preview carries the collision warnings of the panel, as the save prints them into the file.
    expect(lines).not.toBeNull()
    expect(lines?.some((line) => line.includes('UI_OP'))).toBe(true)
    const built = buildProgram(program)
    if (!built.ok) throw new Error(built.problems.join('; '))
    const checked = checkProgram(program, built.path)
    expect(lines).toEqual(emitProgram(program, built.path, checked.warnings))
  })

  it('шаги 4-7. «Добавить операцию»: face + pocket + drill в одной программе, порядок сохранён', () => {
    // Two more: the order holds, tool changes are grouped, M1 before 2 and 3.
    const s = run(withDocument(panel, [FACE], 'DEMO-3'), ...edit(POCKET, { 'rect.x': '-30' }), ...edit(DRILL, {}))
    const { program, preview } = camOf(s)
    const text = (preview ?? []).join('\n')

    expect(program.operations.map((op) => op.kind)).toEqual(['face', 'pocket', 'drill'])
    expect(validate(program).ok).toBe(true)
    expect(text).toContain('G54')
    expect(text).toContain('G43 Z')
    // M1 is a numbered block of the file (`N55 M1`), not a bare line: the stop stands before the
    // second and the third operations, never before the first.
    expect((text.match(/^N\d+ M1$/gm) ?? []).length).toBe(2)
    expect(text.trimEnd().endsWith('%')).toBe(true)
    expect((text.match(/^N\d+ M30$/gm) ?? []).length).toBe(1)
  })

  it('шаги 4-7. «Добавить операцию»: сводка шапки считает документ вместе с открытой формой', () => {
    // The pocket is half typed (not accepted) when the header changes the stock (step 2). The header
    // numbers must describe the screen — the document PLUS the form — not the document alone, and
    // the document alone does not plan, so a header reading it keeps the older numbers.
    const opened = run(panel, { type: 'CAM_OPEN' }, { type: 'CAM_OP', kind: 'pocket' }, ...edit(POCKET).slice(0, -1))
    const cam = camOf(run(opened, { type: 'CAM_PROGRAM', field: 'stock.max.x', value: '60' }))

    expect(cam.form).not.toBeNull()
    expect(cam.program.operations).toHaveLength(0)
    const withForm = formDocument(cam)
    if (withForm === null) throw new Error('the form closed and the document is empty')
    const planForm = programStats(withForm)
    if (planForm === null) throw new Error('the document plus the form does not plan')
    expect(cam.stats).toEqual(planForm)
    expect(cam.stats).not.toEqual(programStats(cam.program))
  })

  it('шаг 8-9. «Проверить»: коллизии и вьювер не находят ошибок у сохранённой программы', () => {
    // «Проверить»: stock check and viewer rules give no error; warnings are allowed and named.
    const { program } = camOf(withDocument(panel, [FACE, POCKET, DRILL], 'DEMO-3'))
    const path = plannedPath(program)
    const collision = checkProgram(program, path)
    const issues = viewerIssues(emitProgram(program, path))

    expect(collision.errors).toEqual([])
    expect(issues.filter((issue) => issue.level === 'error')).toEqual([])
    // The plan of three operations has one span each: the preview the operator saw is not empty.
    expect(path.operations).toHaveLength(3)
    // The warning of the file is the park the viewer reads as a move; it is allowed and named.
    const codes = issues.map((issue) => issue.code)
    expect(codes.every((code) => code === 'park-as-move')).toBe(true)
  })

  it('шаг 10. «Сохранить»: первый раз пишет .ngc и .qprog без вопроса о замене', () => {
    // The first save writes both files, no `.bak`: nothing was replaced.
    const s = run(withDocument(panel, [FACE, POCKET, DRILL], 'DEMO-3'), { type: 'CAM_SAVE', at: 1_000 })

    expect(s.message?.level).not.toBe('error')
    expect(fileOf(s, 'DEMO-3-3.ngc')).not.toBeNull()
    expect(fileOf(s, 'DEMO-3-3.qprog')).not.toBeNull()
    expect(fileOf(s, 'DEMO-3-3.bak')).toBeNull()

    // The `.ngc` is the preview: what was on the screen was saved.
    const ngc = fileOf(s, 'DEMO-3-3.ngc')!.lines
    expect(ngc).toEqual(camOf(s).preview)
    // The header names the program that made the file.
    expect(ngc.some((line) => line.includes('MODEL: DEMO-3'))).toBe(true)
    expect(ngc.some((line) => line.includes('GENERATOR: lynx-touch'))).toBe(true)
  })

  it('шаг 10-11. «Открыть в редакторе»: файл в списке, открыт и видны строки', () => {
    // `FILE_OPEN` on the saved file: the program screen shows its lines.
    const saved = run(withDocument(panel, [FACE, POCKET, DRILL], 'DEMO-3'), { type: 'CAM_SAVE', at: 1_000 })
    const index = saved.files.findIndex((f) => f.name === 'DEMO-3-3.ngc')
    const dialog = run({ ...saved, fileCursor: index }, { type: 'FILE_OPEN' })
    expect(index).toBeGreaterThanOrEqual(0)
    expect(dialog.activeFile).toBe(index)
    expect(dialog.screen).toBe('prog')
    expect(currentFile(dialog).name).toBe('DEMO-3-3.ngc')
    expect(currentFile(dialog).lines.length).toBeGreaterThan(10)
    expect(currentFile(dialog).lines).toEqual(fileOf(saved, 'DEMO-3-3.ngc')!.lines)
    // The header of the overlay offers the same file: the predicate names what the save wrote.
    expect(openTarget(camOf(saved))).toBe('DEMO-3-3.ngc')
  })

  it('шаг 11. «Запустить»: первый пуск сохранённой УП спрашивает, второй запускает', () => {
    // PLAN-QUICKPROG, 8: the saved quick program asks once. The first press only asks, the second
    // press within five seconds starts the run: nothing starts without the operator's second word
    // on a program just written.
    const opened = run(
      withDocument(panel, [FACE, POCKET, DRILL], 'DEMO-3'),
      { type: 'CAM_SAVE', at: 1_000 },
      { type: 'CAM_CLOSE' },
    )
    const index = opened.files.findIndex((f) => f.name === 'DEMO-3-3.ngc')
    const ready = run({ ...opened, fileCursor: index, workplace: 'RUN', run: 'IDLE' }, { type: 'FILE_OPEN' })
    expect(currentFile(ready).name).toBe('DEMO-3-3.ngc')
    expect(isBusy(ready)).toBe(false)

    const asked = run(ready, { type: 'CYCLE_START', at: 2_000 })
    expect(asked.run).toBe('IDLE')
    expect(asked.message?.level).toBe('warn')
    expect(asked.message?.text).toContain('ещё раз')

    const started = run(asked, { type: 'CYCLE_START', at: 5_000 })
    expect(started.run).toBe('RUN')
    expect(isBusy(started)).toBe(true)
    expect(started.message?.level).not.toBe('error')
    // The dry run of the same file asks and starts by the same rule, under its own mode.
    const dry = run({ ...ready, workplace: 'TEST' }, { type: 'CYCLE_START', at: 2_000 })
    expect(dry.run).not.toBe('RUN')
    expect(run(dry, { type: 'CYCLE_START', at: 5_000 }).run).toBe('RUN')
  })

  it('шаг 11. «Запустить»: авария не пускает - CYCLE_START при ESTOP отказывает', () => {
    // A start under the emergency stop: refused with the reason, the run does not start.
    const s = run({ ...panel, run: 'ESTOP' }, { type: 'CYCLE_START' })

    expect(s.run).toBe('ESTOP')
    expect(s.message?.level).toBe('error')
    expect(s.message?.text).toContain('Аварийный стоп')
  })

  it('шаг 10. «Сохранить» при прогоне и аварии: запись УП запрещена', () => {
    // The save reads the same guards: a busy machine and a stop refuse, the files do not change.
    for (const blocked of ['RUN', 'HOLD', 'ESTOP'] as const) {
      const s = withDocument({ ...panel, run: blocked }, [FACE, POCKET, DRILL], 'DEMO-3')
      const after = run(s, { type: 'CAM_SAVE', at: 1_000 })

      expect(after.message?.level).toBe('warn')
      expect(fileOf(after, 'DEMO-3-3.ngc')).toBeNull()
      expect(camOf(after).saved).toBeNull()
    }
  })

  it('шаг 10-11. «Открыть быструю программу»: .qprog читается обратно теми же операциями', () => {
    // The operator saves, then opens the `.qprog` again: the document comes back with the same
    // three operations and the same sizes, and it plans to the same file.
    const s = run(withDocument(panel, [FACE, POCKET, DRILL], 'DEMO-3'), { type: 'CAM_SAVE', at: 1_000 })
    const model = fileOf(s, 'DEMO-3-3.qprog')!.lines.join('\n')
    const read = parseProgram(model)

    expect(read.ok).toBe(true)
    if (!read.ok) return
    expect(read.program.operations.map((op) => op.kind)).toEqual(['face', 'pocket', 'drill'])
    const pocket = read.program.operations[1]
    expect(pocket.kind === 'pocket' ? pocket.rect : null).toEqual({ x: -30, y: -20, w: 60, h: 40 })
    // The text of the file is the one the preview drew: the panel and the disk agree.
    const built = buildProgram(read.program)
    if (!built.ok) throw new Error(built.problems.join('; '))
    expect(emitProgram(read.program, built.path)).toEqual(fileOf(s, 'DEMO-3-3.ngc')!.lines)
  })
})
