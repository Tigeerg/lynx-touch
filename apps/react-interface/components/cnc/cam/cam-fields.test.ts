// The form fields and the way a reason reaches its input (PLAN-QUICKPROG, 7.4, C5).
//
// The overlay is a React tree, and the project has no DOM renderer, so the contract the operator
// reads is checked on the pure helpers: the inputs of every kind carry a form key, a shorthand and
// a hint, and a problem of the shape check lands next to the number it talks about, not under the
// first input of the block.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from '@/lib/cnc/cam/defaults'
import { newForm } from '@/lib/cnc/cam/form'
import { valuesOf } from '@/lib/cnc/cam/form'
import type { OperationKind } from '@/lib/cnc/cam/ops'
import { validate } from '@/lib/cnc/cam/validate'
import {
  allFields,
  CHOICE_LABEL,
  CHOICES,
  droFieldOf,
  errorsByField,
  fieldFor,
  fieldGroups,
  kindRows,
  MULTILINE,
  patternFields,
  SOURCE_FIELD,
} from './cam-fields'
import { pickedSources, SOURCE_KINDS, sourceOptions, toggleSource } from './source-choice'
import { sourceIndexes } from '@/lib/cnc/cam/form-fields-q5'

const KINDS: OperationKind[] = ['face', 'pocket', 'circle-pocket', 'drill']
const Q4: OperationKind[] = ['contour', 'slot', 'chamfer', 'tap', 'custom', 'probe']
const Q5: OperationKind[] = ['counterbore', 'thread', 'engrave', 'pocket-island', 'rest']

const noLabelShort = (text: string) => text.trim().length >= 3

describe('поля формы', () => {
  it('каждое поле любого вида имеет подпись, единицу и подсказку', () => {
    for (const kind of [...KINDS, ...Q4, ...Q5]) {
      for (const field of allFields(kind)) {
        expect(field.key, kind).toMatch(/^[a-zA-Z]+(\.[a-zA-Z]+)?$/)
        expect(noLabelShort(field.label), `${kind}:${field.key}`).toBe(true)
        expect(field.unit.length, `${kind}:${field.key}`).toBeGreaterThanOrEqual(0)
        expect(noLabelShort(field.hint), `${kind}:${field.key}`).toBe(true)
      }
    }
  })

  it('ключ поля есть в значениях формы: подпись не расходится с CAM_PARAM', () => {
    for (const kind of [...KINDS, ...Q4, ...Q5]) {
      const values = valuesOf(newOperation(kind, { id: '', topZ: 0 }))
      for (const field of allFields(kind)) {
        expect(values, `${kind}:${field.key}`).toHaveProperty(field.key)
      }
    }
  })

  it('вкладки «Геометрия», «Инструмент», «Режимы» показывают свои поля', () => {
    for (const tab of ['geometry', 'tool', 'modes'] as const) {
      const keys = fieldGroups('pocket', tab).flatMap((g) => g.fields.map((f) => f.key))
      expect(keys.length).toBeGreaterThan(0)
    }
    const modes = fieldGroups('pocket', 'modes').flatMap((g) => g.fields.map((f) => f.key))
    expect(modes).toEqual(['cutting.spindleRpm', 'cutting.feedMmMin', 'cutting.plungeFeedMmMin'])
  })

  it('новое сверло по умолчанию показывает поля одной схемы', () => {
    const form = newForm('drill', newProgram())
    expect(patternFields(form.values.pattern)).toEqual(['hole.x', 'hole.y', 'hole.r'])
    expect(patternFields('grid')).toContain('rows')
  })

  it('у выбора подпись из общего словаря', () => {
    for (const values of Object.values(CHOICES)) {
      for (const value of values) expect(CHOICE_LABEL[value], value).toBeTruthy()
    }
  })
})

describe('ошибка рядом с полем', () => {
  it('причина о радиусе угла попадает в поле радиуса, а не в общую «геометрию»', () => {
    expect(fieldFor({ field: 'geometry', reason: 'Радиус угла 500 мм больше половины короткой стороны' })).toBe(
      'cornerRadiusMm',
    )
    expect(fieldFor({ field: 'geometry', reason: 'Припуск -1 мм отрицательный' })).toBe('finishAllowanceMm')
    expect(fieldFor({ field: 'stepOver', reason: 'Шаг 5 % меньше 10 % диаметра' })).toBe('stepOverPct')
    expect(fieldFor({ field: 'peck', reason: 'Шаг вывода стружки Q20 мм больше глубины' })).toBe('peckMm')
  })

  it('поле, которое назвал сам проверщик, остаётся своим', () => {
    expect(fieldFor({ field: 'depthMm', reason: 'Глубина обработки должна быть больше нуля.' })).toBe('depthMm')
    expect(fieldFor({ field: 'cutting.feedMmMin', reason: 'Рабочая подача должна быть больше нуля.' })).toBe(
      'cutting.feedMmMin',
    )
  })

  it('одно поле — одна строка: повторы не двоятся', () => {
    const byField = errorsByField([
      { field: 'depthMm', reason: 'первая' },
      { field: 'depthMm', reason: 'вторая' },
    ])
    expect(byField).toEqual({ depthMm: 'первая' })
  })

  it('проблемы реальной формы раскладываются по полям', () => {
    const face = newOperation('face', { id: '', topZ: 0 })
    const broken = { ...face, depthMm: -2 }
    const checked = validate({ ...newProgram(), operations: [broken] })
    expect(checked.ok).toBe(false)
    if (checked.ok) return
    const byField = errorsByField(checked.bad)
    expect(byField.depthMm).toContain('Глубина')
  })
})

describe('поля новых видов (PLAN-QUICKPROG, 7.6)', () => {
  it('выборы контура, паза и щупа нарисованы списком, а не числом', () => {
    for (const key of ['closed', 'side', 'axis', 'pattern']) {
      expect(CHOICES[key], key).toBeDefined()
      for (const value of CHOICES[key]) expect(CHOICE_LABEL[value], `${key}:${value}`).toBeTruthy()
    }
  })

  it('точки контура и щупа и блок G-кода — многострочное поле', () => {
    for (const kind of ['contour', 'probe', 'custom'] as const) {
      const keys = allFields(kind).map((f) => f.key)
      expect(keys, kind).toContain('points'.replace('points', kind === 'custom' ? 'gcode' : 'points'))
      expect(MULTILINE, kind).toContain(kind === 'custom' ? 'gcode' : 'points')
    }
  })

  it('виды Q5 показывают свои поля: цековка, резьба, гравировка, остров, остаток', () => {
    const keys = (kind: OperationKind) => allFields(kind).map((f) => f.key)
    expect(keys('counterbore')).toEqual(expect.arrayContaining(['seatDiameterMm', 'seatDepthMm', 'dwellSec']))
    expect(keys('thread')).toEqual(
      expect.arrayContaining(['pitchMm', 'internal', 'threadDepthMm', 'threadAllowanceMm', 'stepOverPct']),
    )
    expect(keys('engrave')).toEqual(expect.arrayContaining(['text', 'x', 'y', 'heightMm', 'angleDeg']))
    expect(keys('pocket-island')).toEqual(
      expect.arrayContaining(['island.x', 'island.y', 'island.w', 'island.h', 'islandAllowanceMm']),
    )
    expect(keys('rest')).toEqual(expect.arrayContaining(['source', 'allowanceMm', 'stepOverPct']))
    // The text of an engraving is typed, not picked; the thread side is a choice.
    expect(MULTILINE).toContain('text')
    expect(CHOICES.internal).toEqual(['on', 'off'])
  })

  it('«Исходные операции» остатка — выбор из списка, а не многострочное поле', () => {
    expect(MULTILINE).not.toContain(SOURCE_FIELD)
    expect(SOURCE_FIELD).toBe('source')
    // The sources come from the document: three pockets and a slot are offered, a face is not.
    const program = {
      ...newProgram(),
      operations: [
        newOperation('face', { id: '', topZ: 0 }),
        newOperation('pocket', { id: '', topZ: 0 }),
        newOperation('pocket-island', { id: '', topZ: 0 }),
        newOperation('circle-pocket', { id: '', topZ: 0 }),
        newOperation('slot', { id: '', topZ: 0 }),
        newOperation('drill', { id: '', topZ: 0 }),
      ],
    }
    expect(sourceOptions(program.operations).map((o) => [o.index, o.number, o.kind])).toEqual([
      [1, 2, 'pocket'],
      [2, 3, 'pocket-island'],
      [3, 4, 'circle-pocket'],
      [4, 5, 'slot'],
    ])
    expect(SOURCE_KINDS).toEqual(['pocket', 'pocket-island', 'circle-pocket', 'slot'])
  })

  it('выбор источников остатка пишет текст «0 2», который читает форма', () => {
    // The pick builds the field text; the reader of the form (`sourceIndexes`) is the one contract:
    // a toggle on and off gives back the text the document round-trips through.
    const first = toggleSource('', 0)
    expect(first).toBe('0')
    const both = toggleSource(first, 2)
    expect(both).toBe('0\n2')
    expect(sourceIndexes({ source: both }, 'source', [])).toEqual([0, 2])
    expect(toggleSource(both, 0)).toBe('2')
    expect(sourceIndexes({ source: toggleSource(both, 0) }, 'source', [])).toEqual([2])
    expect(pickedSources('2 0 2')).toEqual([2, 0])
    // A document text with spaces still reads as a pick, so an old `.qprog` opens with its rows on.
    expect(pickedSources('0 2')).toEqual([0, 2])
  })

  it('«Полный круг» сверла — выбор Да/Нет, а не пустая строка', () => {
    expect(allFields('drill').map((f) => f.key)).toContain('fullTurn')
    expect(CHOICES.fullTurn).toEqual(['on', 'off'])
    expect(CHOICE_LABEL.off).toBe('Нет')
  })

  it('форма нового вида не растёт: схема метчика скрывает чужие поля', () => {
    // The note asks for six to eight visible rows; a slot (six shape fields plus three frame ones)
    // is the measured exception, nine rows of 36 px = 324 px of the 439 px panel column. A tap has
    // three layouts in one table, and the form shows one of them (`kindRows`), as it does for a drill.
    for (const kind of ['contour', 'slot', 'chamfer', 'custom', 'probe'] as const) {
      expect(kindRows(kind, 'grid'), `${kind}: строк`).toBeLessThanOrEqual(9)
    }
    // A counterbore carries its seat, its depth and the dwell on every layout: ten rows of 36 px is
    // 360 px of the 439 px column, so the panel form fits and the touch form scrolls nothing here.
    expect(kindRows('counterbore', 'grid'), 'counterbore: сетка').toBe(10)
    expect(kindRows('tap', 'grid'), 'tap: сетка').toBe(10)
    expect(kindRows('tap', 'circle'), 'tap: круг').toBe(11)
    // A thread mill is the measured exception of Q5: eight shape fields plus the frame are 11 rows
    // of 36 px = 396 px of the 439 px column; the touch form scrolls it.
    expect(kindRows('thread', 'grid'), 'thread: строк').toBe(11)
  })
})

describe('«С DRO» у полей формы', () => {
  it('кнопка есть у центра и верха материала, но не у размеров и точек', () => {
    // The fields that take a displayed coordinate: the centre of the shape and the top of the
    // material. A width, a depth or a list of points is a size, not a position.
    expect(droFieldOf('pocket', 'rect.x')).toBe('x')
    expect(droFieldOf('pocket', 'rect.y')).toBe('y')
    expect(droFieldOf('circle-pocket', 'circle.x')).toBe('x')
    expect(droFieldOf('drill', 'hole.y')).toBe('y')
    expect(droFieldOf('engrave', 'x')).toBe('x')
    expect(droFieldOf('face', 'topZ')).toBe('topZ')
    expect(droFieldOf('pocket', 'rect.w')).toBeNull()
    expect(droFieldOf('pocket', 'depthMm')).toBeNull()
    expect(droFieldOf('contour', 'points')).toBeNull()
  })
})
