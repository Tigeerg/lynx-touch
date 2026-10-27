// Theme cycle check: "Светлая" is the default theme, every theme is reachable, the cycle closes.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import { THEME_OPTIONS, THEME_PARAM } from './service-data'
import { THEME_CLASSES, themeClasses } from './theme'

const withTheme = (theme: number) => ({ ...initialState, params: { ...initialState.params, [THEME_PARAM]: theme } })
const toggled = (theme: number) => reducer(withTheme(theme), { type: 'THEME_TOGGLE' }).params[THEME_PARAM]

describe('темы оформления', () => {
  it('по умолчанию включена «Светлая»', () => {
    expect(initialState.params[THEME_PARAM]).toBe(THEME_OPTIONS.indexOf('Светлая'))
    expect(THEME_OPTIONS[initialState.params[THEME_PARAM]]).toBe('Светлая')
  })

  it('переключение идёт по кругу от «Светлой» и замыкается', () => {
    expect(THEME_OPTIONS).toHaveLength(6)
    // The cycle below is written in indices: pin the label the default is taken by to index 2.
    expect(THEME_OPTIONS.indexOf('Светлая')).toBe(2)
    const cycle = [2, 3, 4, 5, 0, 1, 2]
    for (let i = 0; i < cycle.length - 1; i += 1) {
      expect(toggled(cycle[i]), `${THEME_OPTIONS[cycle[i]]} -> ${THEME_OPTIONS[cycle[i + 1]]}`).toBe(cycle[i + 1])
    }
  })

  it('переключение без ключа в состоянии берёт следующий после дефолта, а не NaN', () => {
    const s = reducer({ ...initialState, params: {} }, { type: 'THEME_TOGGLE' })

    expect(s.params[THEME_PARAM]).toBe(THEME_OPTIONS.indexOf('Светлая') + 1)
  })
})

describe('классы темы до первого кадра', () => {
  it('каждая тема даёт свои классы, светлая — пустой набор', () => {
    expect(THEME_CLASSES).toHaveLength(THEME_OPTIONS.length)
    expect(themeClasses(0)).toEqual(['theme-minimal'])
    expect(themeClasses(1)).toEqual(['dark', 'theme-minimal', 'theme-minimal-dark'])
    expect(themeClasses(2)).toEqual([])
    expect(themeClasses(3)).toEqual(['dark'])
    expect(themeClasses(4)).toEqual(['dark', 'theme-graphite'])
    expect(themeClasses(5)).toEqual(['theme-mint'])
  })
})

describe('загрузка сохранённой темы', () => {
  it('THEME_SET принимает только номер из каталога, иначе оставляет прежнюю', () => {
    expect(reducer(withTheme(2), { type: 'THEME_SET', value: 5 }).params[THEME_PARAM]).toBe(5)
    expect(reducer(withTheme(2), { type: 'THEME_SET', value: -1 }).params[THEME_PARAM]).toBe(2)
    expect(reducer(withTheme(2), { type: 'THEME_SET', value: 99 }).params[THEME_PARAM]).toBe(2)
  })
})
