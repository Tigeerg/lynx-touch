// Проверка рабочего места АВТО и единой вкладки «Работа»: набор F1-F8, действия
// ресурса/счётчиков/слома и перевод вкладки в экран по рабочему месту.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import { getSoftkeys } from './softkeys'
import { screenOfTab, tabOfScreen } from './types'

describe('единая вкладка «Работа»', () => {
  it('позиция и программа — одна вкладка', () => {
    expect(tabOfScreen('pos')).toBe('work')
    expect(tabOfScreen('prog')).toBe('work')
    expect(tabOfScreen('tool')).toBe('tool')
  })

  it('вкладка «Работа» ведёт на позицию в Наладке и на программу в Прогоне и Авто', () => {
    expect(screenOfTab('work', 'SETUP')).toBe('pos')
    expect(screenOfTab('work', 'TEST')).toBe('prog')
    expect(screenOfTab('work', 'RUN')).toBe('prog')
  })
})

describe('авто: F1-F8 и действия', () => {
  it('в АВТО на экране работы — боевой набор, резерв F7 пуст', () => {
    const types = getSoftkeys(initialState).map((k) => k?.action?.type)

    expect(types).toEqual([
      'FILE_DIALOG',
      'PROG_TO_CURSOR',
      'TOOL_LIFE',
      undefined,
      'PART_COUNTER_RESET',
      'BROKEN_TOOL_CHECK',
      undefined,
      'AUTO_VIEW_TOGGLE',
    ])
  })

  it('смена рабочего места сохраняет текущую вкладку, если она разрешена', () => {
    const kept = reducer({ ...initialState, screen: 'offset' }, { type: 'SET_WORKPLACE', workplace: 'SETUP' })
    const bounced = reducer({ ...initialState, screen: 'offset' }, { type: 'SET_WORKPLACE', workplace: 'TEST' })

    expect(kept.screen).toBe('offset')
    expect(bounced.screen).toBe('prog')
  })

  it('вид в АВТО переключается своим действием', () => {
    const on = reducer(initialState, { type: 'AUTO_VIEW_TOGGLE' })

    expect(on.test.autoCode).toBe(false)
    expect(on.message?.text).toBe('Вид: графика траектории')
  })

  it('счётчики деталей обнуляются в останове', () => {
    const s = reducer({ ...initialState, partCount: 41, cycleTime: 900 }, { type: 'PART_COUNTER_RESET' })

    expect(s.partCount).toBe(0)
    expect(s.cycleTime).toBe(0)
    expect(s.message?.text).toBe('Счётчик деталей и таймер цикла обнулены')
  })

  it('ресурс инструмента выводится сообщением', () => {
    const s = reducer(initialState, { type: 'TOOL_LIFE' })

    expect(s.message?.text).toBe('Ресурс инструмента T01: 72%')
  })
})
