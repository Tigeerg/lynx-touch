// Проверка рабочего места ПРОГОН: переключение шторки, габаритов, очистки графики
// и ракурса, плюс набор F1-F8. Тесты держатся за состояние и слова сообщений.
import { describe, expect, it } from 'vitest'
import { initialState, reducer, type Action } from './machine'
import { getSoftkeys } from './softkeys'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: Action[]) => actions.reduce(reducer, state)
const test = (...actions: Action[]) => run(initialState, { type: 'SET_WORKPLACE', workplace: 'TEST' }, ...actions)

describe('прогон: шторка, габариты, очистка графики, вид', () => {
  it('ПРОГОН включает АВТО и холостой ход', () => {
    const s = test()

    expect(s.workplace).toBe('TEST')
    expect(s.mode).toBe('AUTO')
    expect(s.dryRun).toBe(true)
  })

  it('шторка кода скрывается и возвращается', () => {
    const hidden = test({ type: 'TEST_CURTAIN' })

    expect(hidden.test.curtain).toBe(true)
    expect(test({ type: 'TEST_CURTAIN' }, { type: 'TEST_CURTAIN' }).test.curtain).toBe(false)
  })

  it('габариты УП переключаются с сообщением', () => {
    const on = test({ type: 'TEST_BBOX' })

    expect(on.test.bbox).toBe(true)
    expect(on.message?.text).toBe('Габариты УП: экстремумы X/Y/Z на экране')
  })

  it('очистка графики переключается с сообщением', () => {
    const cleared = test({ type: 'TEST_CLEAR_PATH' })

    expect(cleared.test.hidePath).toBe(true)
    expect(cleared.message?.text).toBe('Графика очищена — F6 вернёт отрисовку')
  })

  it('вид идёт по кругу ISO, XY, XZ', () => {
    const a = test({ type: 'PATH_VIEW_CYCLE' })
    const b = run(a, { type: 'PATH_VIEW_CYCLE' })
    const c = run(b, { type: 'PATH_VIEW_CYCLE' })

    expect([a.test.view, b.test.view, c.test.view]).toEqual(['XY', 'XZ', 'ISO'])
  })

  it('в ПРОГОНЕ F1-F8 — набор проверки УП', () => {
    const labels = getSoftkeys(test()).map((k) => k?.label)

    expect(labels).toEqual([
      'Каталог УП',
      'Редактор',
      'Поиск / Замена',
      'Мат. прогон',
      'Габариты УП',
      'Очистка графики',
      'Вид',
      'Скрыть код',
    ])
  })
})
