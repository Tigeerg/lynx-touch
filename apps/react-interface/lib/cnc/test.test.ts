// PROGON workplace check: the bounding box, the view angle, the run mode F4 cycles (the
// simulation or the dry run), what ПУСК starts in each mode, and the F1-F8 set. The tests hold
// the state and the message words.
import { describe, expect, it } from 'vitest'
import { initialState, reducer, type Action } from './machine'
import { getSoftkeys } from './softkeys'
import { AUTO_VIEWS, type MachineState } from './types'

const run = (state: MachineState, ...actions: Action[]) => actions.reduce(reducer, state)
const test = (...actions: Action[]) => run(initialState, { type: 'SET_WORKPLACE', workplace: 'TEST' }, ...actions)

describe('прогон: габариты и вид', () => {
  it('ПРОГОН включает АВТО и холостой ход', () => {
    const s = test()

    expect(s.workplace).toBe('TEST')
    expect(s.mode).toBe('AUTO')
    expect(s.dryRun).toBe(true)
  })

  it('габариты УП переключаются с сообщением', () => {
    const on = test({ type: 'TEST_BBOX' })

    expect(on.test.bbox).toBe(true)
    expect(on.message?.text).toBe('Габариты УП: экстремумы X/Y/Z на экране')
  })

  it('вид идёт по кругу ISO, XY, XZ', () => {
    const a = test({ type: 'PATH_VIEW_CYCLE' })
    const b = run(a, { type: 'PATH_VIEW_CYCLE' })
    const c = run(b, { type: 'PATH_VIEW_CYCLE' })

    expect([a.test.view, b.test.view, c.test.view]).toEqual(['XY', 'XZ', 'ISO'])
  })

  it('в ПРОГОНЕ F1-F8 — набор проверки УП', () => {
    const keys = getSoftkeys(test())

    expect(keys.map((k) => k?.label)).toEqual([
      'Каталог УП',
      'Редактор',
      'Поиск / Замена',
      'Режим: Холостой',
      'Покадр.',
      'M01',
      'Габариты УП',
      'Отображение: траектория',
    ])
    expect(keys.map((k) => k?.action?.type)).toEqual([
      'FILE_DIALOG',
      'EDIT_MODE',
      'SEARCH',
      'TEST_RUN_MODE',
      'TOGGLE',
      'TOGGLE',
      'TEST_BBOX',
      'AUTO_VIEW_CYCLE',
    ])
    expect(keys[3]?.active).toBe(true)
    expect(keys[4]?.action).toEqual({ type: 'TOGGLE', key: 'singleBlock' })
    expect(keys[4]?.active).toBe(false)
    expect(keys[5]?.action).toEqual({ type: 'TOGGLE', key: 'optStop' })
    expect(keys[5]?.active).toBe(true)
    expect(keys[6]?.active).toBe(false)
  })

  it('режим прогона переключается sim ↔ dry и меняет подпись F4', () => {
    const dry = test()
    const sim = run(dry, { type: 'TEST_RUN_MODE' })

    expect(dry.test.runMode).toBe('dry')
    expect(getSoftkeys(dry)[3]?.label).toBe('Режим: Холостой')
    expect(sim.test.runMode).toBe('sim')
    expect(sim.message?.text).toBe('Режим прогона: Симуляция')
    expect(getSoftkeys(sim)[3]?.label).toBe('Режим: Симуляция')
    expect(getSoftkeys(sim)[3]?.action).toEqual({ type: 'TEST_RUN_MODE' })

    const back = run(sim, { type: 'TEST_RUN_MODE' })

    expect(back.test.runMode).toBe('dry')
    expect(back.message?.text).toBe('Режим прогона: Холостой')
    expect(getSoftkeys(back)[3]?.label).toBe('Режим: Холостой')
  })

  it('ПУСК в режиме «Симуляция» идёт по кадрам, не двигая оси', () => {
    const s = test({ type: 'TEST_RUN_MODE' }, { type: 'CYCLE_START' })

    expect(s.test.runMode).toBe('sim')
    expect(s.sim.active).toBe(true)
    expect(s.sim.line).toBe(s.cursor.prog)
    expect(s.run).toBe('IDLE')
    expect(s.motion).toBeNull()
    expect(s.dryRun).toBe(false)
    expect(s.machine).toEqual(initialState.machine)
    expect(s.message?.text).toBe('Симуляция: прогон без движения осей, кадр 1 — старт')
  })

  it('ПУСК в режиме «Симуляция» на пустой УП даёт предупреждение', () => {
    const empty: MachineState = { ...initialState, files: [{ ...initialState.files[0], lines: ['O1024'] }] }
    const s = run(
      empty,
      { type: 'SET_WORKPLACE', workplace: 'TEST' },
      { type: 'TEST_RUN_MODE' },
      { type: 'CYCLE_START' },
    )

    expect(s.sim.active).toBe(false)
    expect(s.run).toBe('IDLE')
    expect(s.message?.text).toBe('В программе нет кадров для прогона')
    expect(s.message?.level).toBe('warn')
  })

  it('ПУСК в холостом режиме выполняет кадры с dryRun', () => {
    const s = test({ type: 'CYCLE_START' })

    expect(s.test.runMode).toBe('dry')
    expect(s.run).toBe('RUN')
    expect(s.dryRun).toBe(true)
    expect(s.sim.active).toBe(false)
    expect(s.line).toBe(0)
    expect(s.nextLine).toBe(1)
  })

  it('СТОП подачи останавливает симуляцию прогона', () => {
    const s = test({ type: 'TEST_RUN_MODE' }, { type: 'CYCLE_START' }, { type: 'FEED_HOLD' })

    expect(s.sim.active).toBe(false)
    expect(s.message?.text).toBe('Симуляция остановлена')
  })

  it('F8 проходит виды прогона по кругу trajectory, gcode, both, overlay', () => {
    // The cycle must match AUTO_VIEWS step for step: the number of steps and the labels drift
    // apart exactly when one of the two lists is edited alone (the panel would show a view that
    // F8 never selects).
    const seen = [test()]
    for (let i = 1; i < AUTO_VIEWS.length; i += 1) seen.push(run(seen[i - 1], { type: 'AUTO_VIEW_CYCLE' }))
    const back = run(seen[seen.length - 1], { type: 'AUTO_VIEW_CYCLE' })

    expect(seen.map((s) => s.test.autoView)).toEqual(['trajectory', 'gcode', 'both', 'overlay'])
    expect(back.test.autoView).toBe('trajectory')
    expect(seen.map((s) => getSoftkeys(s)[7]?.label)).toEqual([
      'Отображение: траектория',
      'Отображение: gcode',
      'Отображение: совместно',
      'Отображение: УП поверх',
    ])
    expect(seen.slice(1).map((s) => s.message?.text)).toEqual([
      'Отображение: gcode',
      'Отображение: совместно',
      'Отображение: УП поверх',
    ])
    expect(back.message?.text).toBe('Отображение: траектория')
    expect(getSoftkeys(seen[1])[7]?.action).toEqual({ type: 'AUTO_VIEW_CYCLE' })
  })
})
