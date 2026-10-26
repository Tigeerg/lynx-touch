// Block parsing and the modal G-code machine of the program screen. parseWords is the single
// word scanner: the modal codes and the tool list read blocks through it, not through their
// own regexes.
import { describe, expect, it } from 'vitest'
import { modalCodes, parseWords, programTools } from './gcode'

describe('parseWords', () => {
  it('регрессия: буква и число, комментарии выброшены, регистр поднят', () => {
    expect(parseWords('g1 x10.5 y-2 (черновой) F300')).toEqual([
      { letter: 'G', value: 1 },
      { letter: 'X', value: 10.5 },
      { letter: 'Y', value: -2 },
      { letter: 'F', value: 300 },
    ])
  })

  it('регрессия: пустая строка и строка только из комментария дают пустой список', () => {
    expect(parseWords('')).toEqual([])
    expect(parseWords('(только комментарий)')).toEqual([])
  })
})

describe('modalCodes', () => {
  const program = [
    'G21 G90 G17',
    'T2 M6',
    'G0 X0 Y0',
    'S1200 M3',
    'G1 Z-2 F300',
    'G41 D1 X10',
    'G43 H1 Z5',
    'G83 Z-20 R2 Q3',
    'G91 G18 G20 G42 G49 G89',
  ]

  it('по умолчанию — безопасные модальные значения', () => {
    expect(modalCodes(program, -1)).toEqual({
      motion: 0,
      plane: 17,
      unit: 21,
      distance: 90,
      radius: 40,
      length: 49,
      cycle: 80,
    })
  })

  it('учитывает только кадры до executedUpTo включительно', () => {
    expect(modalCodes(program, 0)).toMatchObject({ plane: 17, unit: 21, distance: 90, motion: 0 })
    expect(modalCodes(program, 4)).toMatchObject({ motion: 1, unit: 21, distance: 90 })
    expect(modalCodes(program, 5)).toMatchObject({ radius: 41, length: 49 })
    expect(modalCodes(program, 7)).toMatchObject({ cycle: 83 })
  })

  it('более поздний кадр не учитывается', () => {
    expect(modalCodes(program, 6)).toMatchObject({
      plane: 17,
      unit: 21,
      distance: 90,
      radius: 41,
      length: 43,
      cycle: 80,
    })
    expect(modalCodes(program, 8)).toMatchObject({
      plane: 18,
      unit: 20,
      distance: 91,
      radius: 42,
      length: 49,
      cycle: 89,
    })
  })

  it('выход за длину программы не падает', () => {
    expect(modalCodes(program, 99).cycle).toBe(89)
    expect(modalCodes([], 5).motion).toBe(0)
  })

  it('не путает G-коды соседних диапазонов', () => {
    expect(modalCodes(['G4 P100'], 0)).toMatchObject({ motion: 0, plane: 17, cycle: 80 })
    expect(modalCodes(['G10 L2'], 0)).toMatchObject({ motion: 0, plane: 17 })
    expect(modalCodes(['G90.1'], 0)).toMatchObject({ distance: 90 })
  })
})

describe('programTools', () => {
  it('уникальные номера T в порядке появления', () => {
    const program = ['T2 M6', 'G0 X0 (T9 в комментарии)', 'T5 M6', 'T2 M6', 'T10 M6']
    expect(programTools(program)).toEqual([2, 5, 10])
  })

  it('пустая программа и программа без T дают пустой список', () => {
    expect(programTools([])).toEqual([])
    expect(programTools(['G0 X0', 'M3'])).toEqual([])
  })
})
