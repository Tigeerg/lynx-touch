// Theme cycle check: "Minimal" is the default theme, both variants are reachable, the cycle closes.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import { THEME_OPTIONS, THEME_PARAM } from './service-data'

const withTheme = (theme: number) => ({ ...initialState, params: { ...initialState.params, [THEME_PARAM]: theme } })

describe('темы оформления', () => {
  it('по умолчанию включён светлый «Минимал»', () => {
    expect(THEME_OPTIONS[initialState.params[THEME_PARAM]]).toBe('Минимал · светлая')
  })

  it('светлый и тёмный «Минимал» идут первыми, круг замыкается', () => {
    expect(THEME_OPTIONS).toHaveLength(6)
    expect(reducer(withTheme(0), { type: 'THEME_TOGGLE' }).params[THEME_PARAM]).toBe(1)
    expect(reducer(withTheme(1), { type: 'THEME_TOGGLE' }).params[THEME_PARAM]).toBe(2)
    expect(reducer(withTheme(5), { type: 'THEME_TOGGLE' }).params[THEME_PARAM]).toBe(0)
  })
})
