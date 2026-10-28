// Workplace and warm-up macro check: the behaviour is fixed here, not by eye on the screen.
// The workplace sets the mode, dry run and the set of softkeys.
import { describe, expect, it } from 'vitest'
import { WORKPLACE_LABEL } from './data'
import { initialState, reducer, type Action } from './machine'
import { getSoftkeys } from './softkeys'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: Action[]) => actions.reduce(reducer, state)
const from = (...actions: Action[]) => run(initialState, ...actions)

describe('рабочее место', () => {
  it('НАЛАДКА переводит в РУЧНОЙ и на экран «Позиция»', () => {
    const s = from({ type: 'SET_WORKPLACE', workplace: 'SETUP' })

    expect(s.workplace).toBe('SETUP')
    expect(s.mode).toBe('JOG')
    expect(s.screen).toBe('pos')
    expect(s.dryRun).toBe(false)
    expect(s.message?.text).toBe(`Режим: ${WORKPLACE_LABEL.SETUP}`)
  })

  it('ПРОГОН включает АВТО и холостой ход', () => {
    const s = from({ type: 'SET_WORKPLACE', workplace: 'TEST' })

    expect(s.mode).toBe('AUTO')
    expect(s.dryRun).toBe(true)
  })

  it('АВТО возвращает боевой режим без холостого хода', () => {
    const s = from({ type: 'SET_WORKPLACE', workplace: 'TEST' }, { type: 'SET_WORKPLACE', workplace: 'RUN' })

    expect(s.mode).toBe('AUTO')
    expect(s.dryRun).toBe(false)
  })

  it('смена режима во время выполнения запрещена', () => {
    const s = run({ ...initialState, run: 'RUN' }, { type: 'SET_WORKPLACE', workplace: 'SETUP' })

    expect(s.message?.text).toBe('Смена режима во время выполнения запрещена')
  })

  it('в НАЛАДКЕ на экране позиции F1-F8 — набор подготовки', () => {
    const s = from({ type: 'SET_WORKPLACE', workplace: 'SETUP' })
    const labels = getSoftkeys(s).map((k) => k?.label)

    expect(labels).toEqual([
      'Обмер инструмента',
      '3D щуп',
      'Запись базы',
      // The base table is a tab now; the slot stays as a reserve.
      'Резерв',
      'Безопасный отвод',
      'В ноль детали',
      'Прогрев',
      'Калькулятор',
    ])
  })
})

describe('прогрев шпинделя', () => {
  it('загружает программу прогрева и запускает её в АВТО', () => {
    const s = run({ ...initialState, toolClamped: true }, { type: 'SPINDLE_WARMUP' })

    expect(s.run).toBe('RUN')
    expect(s.workplace).toBe('RUN')
    expect(s.mode).toBe('AUTO')
    expect(s.files[s.activeFile].title).toMatch(/ПРОГРЕВ/i)
    expect(s.message?.text).toBe('Прогрев шпинделя запущен')
  })

  it('при разжатом инструменте прогрев запрещён', () => {
    const s = run({ ...initialState, toolClamped: false }, { type: 'SPINDLE_WARMUP' })

    expect(s.message?.text).toBe('Инструмент разжат — прогрев запрещён')
  })

  it('со щупом в шпинделе прогрев запрещён', () => {
    const s = run({ ...initialState, tool: 99 }, { type: 'SPINDLE_WARMUP' })

    expect(s.message?.text).toBe('В шпинделе щуп T99 — прогрев запрещён')
  })
})
