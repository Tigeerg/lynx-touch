// The words and the numbers the overlay reads (PLAN-QUICKPROG, 7.4, C5).
//
// The project has no DOM renderer (`@testing-library/react` is not a dependency and the task keeps
// it that way), so what is checked here is the contract the markup prints: the kinds are named,
// a card carries the tool, the size and the depth, and the header carries the offset, the
// tolerance and the stock. The layout itself is checked by a screenshot, not by a test.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from '@/lib/cnc/cam/defaults'
import { fieldFor } from './cam-fields'
import { valuesOf } from '@/lib/cnc/cam/form'
import type { OperationKind } from '@/lib/cnc/cam/ops'
import {
  formatMm,
  hasKindWarning,
  headerText,
  KIND_LABEL,
  keySize,
  kindWarnings,
  programSummary,
  toolText,
  warningText,
} from './cam-labels'

const KINDS: OperationKind[] = [
  'face',
  'pocket',
  'circle-pocket',
  'drill',
  'contour',
  'slot',
  'chamfer',
  'tap',
  'engrave',
  'pocket-island',
  'rest',
  'custom',
  'probe',
]

describe('слова оверлея', () => {
  it('у каждого вида операции есть русское название из записки', () => {
    expect(KINDS.map((k) => KIND_LABEL[k])).toEqual([
      'Плоскость',
      'Карман',
      'Круглый карман',
      'Отверстия',
      'Контур',
      'Паз',
      'Фаска',
      'Метчик',
      'Гравировка',
      'Карман с островом',
      'Остаточная обработка',
      'Custom G Code',
      'Блок щупа',
    ])
  })

  it('карточка нового вида читает его ключевой размер', () => {
    const contour = newOperation('contour', { id: '', topZ: 0 })
    expect(keySize(contour)).toContain('замкнут')
    const slot = newOperation('slot', { id: '', topZ: 0 })
    expect(keySize(slot)).toContain('шир.')
    const probe = newOperation('probe', { id: '', topZ: 0 })
    expect(keySize(probe)).toContain('мм')
  })

  it('карточка новых видов читает текст и два прямоугольника', () => {
    const engrave = newOperation('engrave', { id: '', topZ: 0 })
    expect(keySize(engrave)).toBe('«ТЕКСТ» 5 мм')
    const island = newOperation('pocket-island', { id: '', topZ: 0 })
    expect(keySize(island)).toBe('60×40 · остров 20×20')
    const rest = newOperation('rest', { id: '', topZ: 0 })
    expect(keySize(rest)).toBe('вся площадь · нет исходных')
    expect(keySize({ ...rest, rect: { x: 0, y: 0, w: 60, h: 40 }, source: [0, 1] })).toBe('60×40 · из 1, 2')
  })

  it('предупреждения остатка читаются из документа, а не из одной операции', () => {
    const rest = newOperation('rest', { id: '', topZ: 0 })
    expect(kindWarnings(rest)).toEqual([])
    const program = { ...newProgram(), operations: [rest] }
    expect(kindWarnings(rest, program)).toContain(
      'Нет исходных операций: остаток не определён, программа пойдёт по всей площади',
    )
  })

  it('ошибка гравировки находит поле текста, ошибка острова — поле острова', () => {
    expect(fieldFor({ field: 'text', reason: 'Символ «@» не поддержан.' })).toBe('text')
    expect(fieldFor({ field: 'heightMm', reason: 'Высота надписи должна быть больше нуля.' })).toBe('heightMm')
    expect(fieldFor({ field: 'island', reason: 'Остров не помещается: зазор меньше фрезы.' })).toBe('island')
  })

  it('предупреждения не блокируют запись и не подменяют ошибку', () => {
    const slot = newOperation('slot', { id: '', topZ: 0 })
    expect(kindWarnings(slot)).toEqual([])
    const wide = { ...slot, widthMm: slot.tool.diameterMm * 3 } as typeof slot
    expect(kindWarnings(wide)[0]).toContain('двух диаметров')
    const face = newOperation('face', { id: '', topZ: 0 })
    expect(kindWarnings(face)).toEqual([])
    expect(warningText(['a', 'b'])).toBe('Проверьте: a b')
  })

  it('карточка читает инструмент и ключевой размер', () => {
    const pocket = newOperation('pocket', { id: '', topZ: 0 })
    expect(toolText(pocket)).toBe('T01 ф10')
    expect(keySize(pocket)).toBe('60×40')
    const circle = newOperation('circle-pocket', { id: '', topZ: 0 })
    expect(keySize(circle)).toBe('Ø20')
    const drill = newOperation('drill', { id: '', topZ: 0 })
    expect(keySize(drill)).toBe('Ø10 ×1')
  })

  it('шапка показывает смещение, допуск G64 и заготовку без хвостовых нулей', () => {
    const head = headerText(newProgram())
    expect(head.wcs).toBe('G54')
    expect(head.g64).toBe('G64 P0.01 Q0.01')
    expect(head.stock).toBe('100×80×20 · Z 0')
  })

  it('миллиметры печатаются без лишних нулей', () => {
    expect(formatMm(10)).toBe('10')
    expect(formatMm(2.5)).toBe('2.5')
    expect(formatMm(0.1 + 0.2)).toBe('0.3')
  })

  it('каждая форма открывается с полями, которые рисует форма операции', () => {
    for (const kind of KINDS) {
      const values = valuesOf(newOperation(kind, { id: '', topZ: 0 }))
      expect(values.depthMm, kind).toBeTypeOf('string')
    }
  })
})

describe('значок предупреждений карточки', () => {
  it('зависит от предупреждений вида, а не от константы', () => {
    // The card draws the warning sign only when `hasKindWarning` is true: a face has no warning of
    // its kind, a slot wider than two diameters has one. The predicate is the card's own condition,
    // so a sign that is always painted (or never painted) fails here.
    const face = newOperation('face', { id: 'Плоскость', topZ: 0 })
    expect(hasKindWarning(face, newProgram())).toBe(false)
    const slot = newOperation('slot', { id: 'Паз', topZ: 0 })
    const wide = { ...slot, widthMm: slot.tool.diameterMm * 3 }
    expect(hasKindWarning(wide, newProgram())).toBe(true)
    // A rest warning needs the document: the same operation flips the sign with the program.
    const rest = newOperation('rest', { id: 'Остаток', topZ: 0 })
    expect(hasKindWarning(rest, { ...newProgram(), operations: [rest] })).toBe(true)
  })
})

describe('строка оценки в шапке', () => {
  it('читает ходы, длину и время в минутах и секундах', () => {
    expect(programSummary({ moves: 42, lengthMm: 1234.5, seconds: 200 })).toBe(
      'ходов 42 · длина 1234.5 мм · ≈ 03:20 мин',
    )
    expect(programSummary({ moves: 1, lengthMm: 0, seconds: 0 })).toBe('ходов 1 · длина 0 мм · ≈ 00:00 мин')
    expect(programSummary(null)).toBeNull()
  })

  it('минуты округляются к ближайшей секунде, а не отбрасываются', () => {
    expect(programSummary({ moves: 1, lengthMm: 1, seconds: 59.6 })).toContain('01:00 мин')
  })
})

describe('the header line of the overlay', () => {
  // The overlay prints this line from `cam.stats`; the state is covered by the reducer tests, but
  // nothing used to check that the line itself appears with the numbers of the plan.
  it('prints the moves, the length and the time of the plan', () => {
    expect(programSummary({ moves: 289, lengthMm: 2948.9, seconds: 613 })).toBe(
      'ходов 289 · длина 2948.9 мм · ≈ 10:13 мин',
    )
  })

  it('says nothing before the first plan', () => {
    expect(programSummary(null)).toBeNull()
  })
})
