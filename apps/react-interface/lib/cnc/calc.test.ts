// The cutting-mode calculator layer: formulas and defaults live here, not in the overlay.
import { describe, expect, it } from 'vitest'
import { CALC_DEFAULTS, cuttingFeed, cuttingRpm } from './calc'

describe('cuttingRpm', () => {
  it('Vc 150 м/мин на фрезе D 10 мм — 4775 об/мин', () => {
    expect(cuttingRpm(150, 10)).toBe(4775)
  })

  it('нулевая скорость резания даёт 0', () => {
    expect(cuttingRpm(0, 10)).toBe(0)
  })

  it('нулевой диаметр даёт 0, а не деление на ноль', () => {
    expect(cuttingRpm(150, 0)).toBe(0)
  })

  it('отрицательные входы дают 0', () => {
    expect(cuttingRpm(-150, 10)).toBe(0)
    expect(cuttingRpm(150, -10)).toBe(0)
  })
})

describe('cuttingFeed', () => {
  it('fz 0.06 на 3 зуба при 4775 об/мин — 860 мм/мин', () => {
    expect(cuttingFeed(0.06, 3, 4775)).toBe(860)
  })

  it('нули в любом множителе дают 0', () => {
    expect(cuttingFeed(0, 3, 4775)).toBe(0)
    expect(cuttingFeed(0.06, 0, 4775)).toBe(0)
    expect(cuttingFeed(0.06, 3, 0)).toBe(0)
  })
})

describe('CALC_DEFAULTS', () => {
  it('держит текущие значения по умолчанию калькулятора', () => {
    expect(CALC_DEFAULTS).toEqual({ vc: 150, d: 10, fz: 0.06, flutes: 3 })
  })
})
