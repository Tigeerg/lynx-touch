// PROGON workplace check: the bounding box, the view angle, the run modes F4 cycles (simulation,
// dry run, step run, handwheel run), what ПУСК starts in each mode, and the F1-F8 set. The tests
// hold the state and the message words.
import { describe, expect, it } from 'vitest'
import { initialState, reducer, type Action } from './machine'
import { getSoftkeys } from './softkeys'
import { AUTO_VIEWS, type MachineState } from './types'

const run = (state: MachineState, ...actions: Action[]) => actions.reduce(reducer, state)
const test = (...actions: Action[]) => run(initialState, { type: 'SET_WORKPLACE', workplace: 'TEST' }, ...actions)

// F4 starts on the dry run: one press selects the step run, two the handwheel run, three the
// simulation. The named helpers keep the tests about behaviour instead of press counts.
const toStep = (state: MachineState) => run(state, { type: 'TEST_RUN_MODE' })
const toMpg = (state: MachineState) => run(toStep(state), { type: 'TEST_RUN_MODE' })
const toSim = (state: MachineState) => run(toMpg(state), { type: 'TEST_RUN_MODE' })

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

  it('режим прогона идёт по кругу: Холостой, По шагам, По маховику, Симуляция', () => {
    const dry = test()
    const step = toStep(dry)
    const mpg = toMpg(dry)
    const sim = toSim(dry)
    const back = run(sim, { type: 'TEST_RUN_MODE' })

    expect([dry, step, mpg, sim].map((s) => s.test.runMode)).toEqual(['dry', 'step', 'mpg', 'sim'])
    expect([dry, step, mpg, sim].map((s) => getSoftkeys(s)[3]?.label)).toEqual([
      'Режим: Холостой',
      'Режим: По шагам',
      'Режим: По маховику',
      'Режим: Симуляция',
    ])
    expect(step.message?.text).toBe('Режим прогона: По шагам')
    expect(mpg.message?.text).toBe('Режим прогона: По маховику')
    expect(sim.message?.text).toBe('Режим прогона: Симуляция')
    expect(getSoftkeys(mpg)[3]?.action).toEqual({ type: 'TEST_RUN_MODE' })
    expect(back.test.runMode).toBe('dry')
    expect(back.message?.text).toBe('Режим прогона: Холостой')
  })

  it('холостой ход включён только у холостого прогона и у прогона по маховику', () => {
    expect([test().dryRun, toStep(test()).dryRun, toMpg(test()).dryRun, toSim(test()).dryRun]).toEqual([
      true,
      false,
      true,
      false,
    ])
  })

  it('ПУСК в режиме «По шагам» выполняет один кадр и останавливается', () => {
    const step = toStep(test())
    const started = run(step, { type: 'CYCLE_START' })

    expect(started.run).toBe('RUN')
    expect(started.stopAfterBlock).toBe(true)
    // The step run stops the block itself: the F5 single-block key is not silently switched on.
    expect(started.singleBlock).toBe(false)
    expect(started.message?.text).toBe('По шагам: кадр 1. ПУСК — следующий кадр')

    let done = started
    for (let i = 0; i < 400 && done.run === 'RUN'; i += 1) done = reducer(done, { type: 'TICK', dt: 50 })

    expect(done.run).toBe('IDLE')
    // The run stopped on the same block instead of walking on to the next one.
    expect(done.line).toBe(started.line)
    expect(done.nextLine).toBe(started.line + 1)
  })

  it('ПУСК в режиме «По маховику» взводит прогон, кадры идут поворотом маховика', () => {
    const mpg = toMpg(test())
    const armed = run(mpg, { type: 'CYCLE_START' })

    expect(armed.run).toBe('IDLE')
    expect(armed.dryRun).toBe(true)
    expect(armed.message?.text).toBe('Прогон маховиком: кадр 1. Вращайте маховик')

    const turned = run(armed, { type: 'MPG_HANDWHEEL', dir: 1 })

    expect(turned.line).toBe(3)
    expect(turned.message?.text).toBe('Прогон маховиком: кадр 4')
  })

  it('ПУСК в режиме «Симуляция» идёт по кадрам, не двигая оси', () => {
    const s = run(toSim(test()), { type: 'CYCLE_START' })

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
    const s = run(toSim(run(empty, { type: 'SET_WORKPLACE', workplace: 'TEST' })), { type: 'CYCLE_START' })

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
    const s = run(toSim(test()), { type: 'CYCLE_START' }, { type: 'FEED_HOLD' })

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
