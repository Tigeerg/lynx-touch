// Reducer layer check: the machine behaviour is fixed here, not by eye on the screen.
// The tests hold the machine state and the message words - what the operator sees.
import { describe, expect, it } from 'vitest'
import { currentFile, initialState } from './machine'
import { run } from './machine.test-helpers'
import { getSoftkeys } from './softkeys'
import { buildToolpath, simPose } from './toolpath'
import type { MachineState } from './types'

describe('экран «Программа»: поиск, симуляция, запуск', () => {
  const program = (lines: string[]): MachineState => ({
    ...initialState,
    files: [{ name: 'A.NGC', title: 'Тест', lines, modified: '' }],
    activeFile: 0,
    screen: 'prog',
  })

  it('SEARCH_FIND переносит курсор на следующий кадр со строкой', () => {
    const s = run(
      program(['G90 G54', 'T2 M6', 'T5 M6', 'G1 X0 F100']),
      { type: 'SEARCH_QUERY', value: 'T5' },
      { type: 'SEARCH_FIND' },
    )

    expect(s.cursor.prog).toBe(2)
  })

  it('SEARCH_FIND без находки сообщает об ошибке', () => {
    const s = run(program(['G90 G54', 'T2 M6']), { type: 'SEARCH_QUERY', value: 'T9' }, { type: 'SEARCH_FIND' })

    expect(s.cursor.prog).toBe(initialState.cursor.prog)
    expect(s.message?.text).toContain('Не найдено')
  })

  it('SEARCH_REPLACE_ALL заменяет без учёта регистра во всех кадрах', () => {
    const s = run(
      program(['G90 G54', 'T2 M6', 'M6 T2']),
      { type: 'SEARCH_QUERY', value: 't2' },
      { type: 'SEARCH_REPLACE', value: 'T7' },
      { type: 'SEARCH_REPLACE_ALL' },
    )

    expect(currentFile(s).lines).toEqual(['G90 G54', 'T7 M6', 'M6 T7'])
    expect(s.message?.text).toBe('Заменено в кадрах: 2')
  })

  it('SEARCH_REPLACE_ALL вставляет замену буквально, а не как шаблон $', () => {
    const s = run(
      program(['T2 M6']),
      { type: 'SEARCH_QUERY', value: 'T2' },
      { type: 'SEARCH_REPLACE', value: '$&' },
      { type: 'SEARCH_REPLACE_ALL' },
    )

    expect(currentFile(s).lines).toEqual(['$& M6'])
  })

  it('PROG_RENUMBER выстраивает кадры в ряд N10/N20/N30', () => {
    const s = run(program(['G90', 'N7 G1 X0', 'T2 M6']), { type: 'PROG_RENUMBER' })

    expect(currentFile(s).lines).toEqual(['N10 G90', 'N20 G1 X0', 'N30 T2 M6'])
    expect(s.message?.text).toBe('Перенумеровано кадров: 3')
  })

  it('PROG_RENUMBER сохраняет ведущий отступ кадра', () => {
    const s = run(program(['  G90', '  N7 G1 X0']), { type: 'PROG_RENUMBER' })

    expect(currentFile(s).lines).toEqual(['  N10 G90', '  N20 G1 X0'])
    expect(s.message?.text).toBe('Перенумеровано кадров: 2')
  })

  it('SIM: симуляция идёт по кадрам на TICK, оси не двигаются', () => {
    const s0 = run({ ...initialState, screen: 'prog' }, { type: 'SIM' })

    expect(s0.sim.active).toBe(true)
    expect(s0.sim.line).toBe(s0.cursor.prog)

    const s1 = run(s0, { type: 'TICK', dt: 130 }, { type: 'TICK', dt: 130 })

    expect(s1.sim.line).toBe(s0.sim.line + 2)
    expect(s1.run).toBe('IDLE')
    expect(s1.machine).toEqual(initialState.machine)
  })

  it('SIM не запускается во время выполнения', () => {
    const s = run({ ...initialState, screen: 'prog', run: 'RUN' }, { type: 'SIM' })

    expect(s.sim.active).toBe(false)
    expect(s.message?.text).toContain('Остановите')
  })

  it('CYCLE_START гасит симуляцию, чтобы DRO не показывал прогон поверх движения', () => {
    const s = run({ ...initialState, screen: 'prog' }, { type: 'SIM' }, { type: 'CYCLE_START' })

    expect(s.sim.active).toBe(false)
    expect(s.run).toBe('RUN')
  })

  it('PROG_TO_CURSOR восстанавливает модальные G/M-коды из кадров перед стартом', () => {
    const s = run(
      program(['G90 G55 F120 S1200 M3 M8', 'G1 X10 Y10', 'G0 Z5', 'M30']),
      { type: 'CURSOR', delta: 2 },
      { type: 'PROG_TO_CURSOR' },
    )

    expect(s.line).toBe(2)
    expect(s.activeOffset).toBe(1)
    expect(s.spindleSet).toBe(1200)
    expect(s.spindleOn).toBe(true)
    expect(s.coolant).toBe(true)
    expect(s.motion).toBeNull()
    expect(s.message?.text).toContain('G55')
    expect(s.message?.text).toContain('M3')
  })

  it('simPose выдаёт остаток до следующей точки маршрута', () => {
    const segs = buildToolpath(['G1 X10 F100', 'G1 Y10'])

    expect(simPose(segs, 0)).toEqual({ pos: { X: 0, Y: 0, Z: 0 }, dtg: { X: 10, Y: 10, Z: 0 } })
  })

  it('раскладка «Программа»: без дублей физических тумблеров, с поиском/симуляцией/видом', () => {
    const keys = getSoftkeys({ ...initialState, workplace: 'SETUP', screen: 'prog' })

    // PLAN-QUICKPROG 7.4 adds a ninth action to this screen: the quick program opens an overlay
    // from here, so its key belongs to the bar. The bar has eight slots (SOFTKEY_SLOTS), so the key
    // takes the eighth place and the view switch yields it while the overlay is closed for the new
    // key and takes it back while the overlay is open, so the close key never slides the bar. No
    // operator action is dropped: the editor, the search, the run, the renumber, the rewind and
    // the start-from-cursor all keep their keys.
    expect(keys.map((k) => k?.action?.type)).toEqual([
      'FILE_DIALOG',
      'EDIT_MODE',
      'SEARCH',
      'SIM',
      'PROG_RENUMBER',
      'PROG_REWIND',
      'PROG_TO_CURSOR',
      'CAM_OPEN',
    ])
    expect(keys.some((k) => k?.action?.type === 'TOGGLE')).toBe(false)
  })

  it('при открытом поиске софт-кеи — найти/заменить/закрыть', () => {
    const keys = getSoftkeys({ ...initialState, screen: 'prog', search: { open: true, query: 'T5', replace: '' } })

    expect(keys.map((k) => k?.action?.type).slice(0, 3)).toEqual(['SEARCH_FIND', 'SEARCH_REPLACE_ALL', 'SEARCH'])
  })
})
