// The copying dialogs, checked on their pure part (PLAN-QUICKPROG, 7.6): the project has no DOM
// renderer, so the numbers the operator types are turned into `CAM_ARRAY`/`CAM_ROTATE` here and the
// refusal the dialog knows before the document does is read as text. What the document refuses on
// its own (a kind that is not copied, a pocket off a quarter-turn, the 100-copy limit) is the
// reducer's word (`copy-ops.ts`) and shows up as the overlay message; the two limit cases are
// checked here as the dialog's share.
import { describe, expect, it } from 'vitest'

import {
  ARRAY_DEFAULT,
  ARRAY_FIELDS,
  arrayAction,
  COPY_MENU,
  dialogRefusal,
  ROTATE_DEFAULT,
  ROTATE_FIELDS,
  rotateAction,
} from './cam-dialogs'

describe('массив: строки → CAM_ARRAY', () => {
  it('поля по умолчанию дают сетку 2x2 с шагом 20 мм', () => {
    const result = arrayAction(3, ARRAY_DEFAULT)
    expect(result).toEqual({
      ok: true,
      action: { type: 'CAM_ARRAY', index: 3, cols: 2, rows: 2, dx: 20, dy: 20 },
    })
  })

  it('запятая как десятичный разделитель читается, как точка', () => {
    const result = arrayAction(0, { cols: '3', rows: '1', dx: '12,5', dy: '-20' })
    expect(result.ok && result.action).toEqual({ type: 'CAM_ARRAY', index: 0, cols: 3, rows: 1, dx: 12.5, dy: -20 })
  })

  it('не число называется своим полем, а не угадывается', () => {
    for (const [field, label] of [
      ['cols', 'Столбцов'],
      ['rows', 'Рядов'],
      ['dx', 'Шаг по X'],
      ['dy', 'Шаг по Y'],
    ] as const) {
      const result = arrayAction(0, { ...ARRAY_DEFAULT, [field]: 'а' })
      expect(result.ok).toBe(false)
      if (result.ok) return
      expect(result.problem.key).toBe(field)
      expect(result.problem.reason).toContain(label)
    }
    const empty = arrayAction(0, { ...ARRAY_DEFAULT, cols: '  ' })
    expect(empty.ok).toBe(false)
  })
})

describe('поворот: строки → CAM_ROTATE', () => {
  it('поля по умолчанию дают три копии по 90° вокруг начала', () => {
    const result = rotateAction(1, ROTATE_DEFAULT)
    expect(result).toEqual({
      ok: true,
      action: { type: 'CAM_ROTATE', index: 1, angleDeg: 90, cx: 0, cy: 0, copies: 3 },
    })
  })

  it('угол и центр читаются как числа, копии — целое из поля', () => {
    const result = rotateAction(2, { angleDeg: '45', cx: '-10', cy: '7.5', copies: '5' })
    expect(result.ok && result.action).toEqual({
      type: 'CAM_ROTATE',
      index: 2,
      angleDeg: 45,
      cx: -10,
      cy: 7.5,
      copies: 5,
    })
  })

  it('не число в любом поле поворота — отказ с именем поля', () => {
    for (const field of ROTATE_FIELDS) {
      const result = rotateAction(0, { ...ROTATE_DEFAULT, [field.key]: 'x' })
      expect(result.ok).toBe(false)
      if (result.ok) return
      expect(result.problem.key).toBe(field.key)
    }
  })
})

describe('отказ, который знает сам диалог', () => {
  it('сетка 1 на 1 и дробное число копий — отказ строки', () => {
    const single = arrayAction(0, { cols: '1', rows: '1', dx: '0', dy: '0' })
    expect(single.ok && dialogRefusal(single.action)?.key).toBe('cols')
    const fraction = arrayAction(0, { cols: '2.5', rows: '2', dx: '10', dy: '10' })
    expect(fraction.ok && dialogRefusal(fraction.action)?.reason).toContain('целое')
  })

  it('больше 100 копий — отказ до отправки действия', () => {
    const grid = arrayAction(0, { cols: '11', rows: '10', dx: '10', dy: '10' })
    const refusal = grid.ok ? dialogRefusal(grid.action) : null
    expect(refusal).not.toBeNull()
    expect(refusal?.reason).toContain('100')
    expect(refusal?.key).toBe('rows')

    const turn = rotateAction(0, { ...ROTATE_DEFAULT, copies: '100' })
    const turnRefusal = turn.ok ? dialogRefusal(turn.action) : null
    expect(turnRefusal?.key).toBe('copies')
    // 100 turns plus the source is 101 operations: one over the limit of the document.
    expect(turnRefusal?.reason).toContain('вместе')
  })

  it('допустимые числа проходят без отказа', () => {
    const grid = arrayAction(0, { cols: '10', rows: '10', dx: '10', dy: '10' })
    expect(grid.ok && dialogRefusal(grid.action)).toBeNull()
    const turn = rotateAction(0, { ...ROTATE_DEFAULT, copies: '99' })
    expect(turn.ok && dialogRefusal(turn.action)).toBeNull()
  })
})

describe('слова диалогов', () => {
  it('у пункта меню карточки есть подпись и порядок: «Массив», затем «Поворот»', () => {
    expect(COPY_MENU.map((item) => item.label)).toEqual(['Массив', 'Поворот'])
    expect(COPY_MENU.map((item) => item.kind)).toEqual(['array', 'rotate'])
  })

  it('у каждого поля диалога есть подпись, единица и подсказка', () => {
    for (const field of [...ARRAY_FIELDS, ...ROTATE_FIELDS]) {
      expect(field.label.length, field.key).toBeGreaterThan(1)
      expect(field.hint.length, field.key).toBeGreaterThan(5)
    }
  })
})
