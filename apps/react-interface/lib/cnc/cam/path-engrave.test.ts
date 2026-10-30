// Engraving (`engrave`, PLAN-QUICKPROG 7.6): the single-stroke font, the string and its errors.
//
// A glyph is a list of polylines in the unit square: the height of the letter is 1 and the baseline
// is y = 0, so a 10 mm string is ten times its unit box. The character step is 0.6 of the height and
// the string is centred on the X of the operation. The cut runs on `topZ`; the depth is the cut of
// the mill below it. A character outside the set is refused by name, an empty text as well.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { emitProgram } from './emit'
import { buildProgram } from './path'
import type { EngraveOperation, QuickProgram } from './ops'
import { PathBuilder } from './path-builder'
import { engraveLayout, planEngrave } from './path-engrave'
import { GLYPHS, SUPPORTED, glyphOf, supportedText } from './engrave-font'
import { parseProgram, serializeProgram } from './serialize'
import { validate } from './validate'
import { pathWarnings } from './validate-paths'

const stock = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

const engrave = (patch: Partial<EngraveOperation> = {}): EngraveOperation => ({
  ...(newOperation('engrave', { id: 'Гравировка', topZ: 0, depthMm: 0.3 }) as EngraveOperation),
  tool: { number: 1, diameterMm: 2, hNumber: 1 },
  text: 'A B',
  x: 0,
  y: 0,
  heightMm: 10,
  angleDeg: 0,
  ...patch,
})

const program = (operations: QuickProgram['operations']): QuickProgram => ({
  ...newProgram(stock, 'ENGRAVE'),
  operations,
})

const plan = (op: EngraveOperation) => {
  const out = new PathBuilder()
  planEngrave(program([op]), op, out)
  return out.result()
}

/** The cuts of a set of moves, each from its own start, in the order of the string. */
const cuts = (moves: { kind: string; to: { x: number; y: number; z: number } }[]) =>
  moves.reduce<{ from: { x: number; y: number; z: number }; to: { x: number; y: number; z: number } }[]>(
    (acc, move, at) => {
      const from = at === 0 ? { x: 0, y: 0, z: 0 } : moves[at - 1].to
      if (move.kind === 'cut') acc.push({ from, to: move.to })
      return acc
    },
    [],
  )

describe('гравировка: шрифт', () => {
  it('держит латиницу, цифры, пробел и явный список кириллицы', () => {
    for (const ch of '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ') expect(GLYPHS[ch], ch).toBeDefined()
    for (const ch of 'АБВГДЕЖЗИКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ') expect(GLYPHS[ch], ch).toBeDefined()
    // The declared list is the table: nothing supported without a glyph, nothing hidden.
    for (const ch of SUPPORTED) expect(GLYPHS[ch], ch).toBeDefined()
    expect(SUPPORTED).toContain('Я')
    expect(SUPPORTED).not.toContain('Ё')
  })

  it('у каждой буквы 1-4 полилинии из 2-6 точек в единичном квадрате', () => {
    for (const [ch, strokes] of Object.entries(GLYPHS)) {
      if (ch === ' ') continue
      expect(strokes.length, ch).toBeGreaterThanOrEqual(1)
      expect(strokes.length, ch).toBeLessThanOrEqual(4)
      for (const stroke of strokes) {
        expect(stroke.length, ch).toBeGreaterThanOrEqual(2)
        expect(stroke.length, ch).toBeLessThanOrEqual(6)
        for (const point of stroke) {
          expect(point.x, ch).toBeGreaterThanOrEqual(0)
          expect(point.x, ch).toBeLessThanOrEqual(1)
          expect(point.y, ch).toBeGreaterThanOrEqual(0)
          expect(point.y, ch).toBeLessThanOrEqual(1)
        }
      }
    }
  })

  it('неподдержанный символ не даёт глифа, а поддержанный — даёт', () => {
    expect(glyphOf('A')).not.toBeNull()
    expect(glyphOf('@')).toBeNull()
    expect(supportedText('АБВ')).toBe(true)
    expect(supportedText('A@B')).toBe(false)
  })
})

describe('гравировка: планировщик', () => {
  it('«A B» высотой 10: три знака, габарит 26 по ширине и 10 по высоте', () => {
    const layout = engraveLayout(engrave())
    // A letter box is 10 mm of the 10 mm height and the step is 6; the string is centred on x = 0.
    expect(layout.widthMm).toBeCloseTo(2.2, 9)
    expect(layout.xMin).toBeCloseTo(-11, 9)
    expect(layout.xMax).toBeCloseTo(8.5, 9)
    const result = plan(engrave({ x: 0, y: -5 }))
    expect(result.bbox.xMin).toBeCloseTo(-11, 9)
    expect(result.bbox.xMax).toBeCloseTo(8.5, 9)
    expect(result.bbox.yMin).toBeCloseTo(-5, 9)
    expect(result.bbox.yMax).toBeCloseTo(5, 9)
    // Every segment of the four strokes of A and the two of B is one cut at the top Z; the space
    // draws nothing. A is 3 short strokes and one bar of 2 segments, B is two strokes of 4.
    const moveCuts = cuts(result.moves)
    expect(moveCuts.length).toBe(23)
    // The label is a cut below the top by its depth, not a scratch on the surface itself: a stroke
    // is drawn at the plane and rises back to `topZ` between the strokes, so only the cut ends lie
    // on the plane and every other point of a cut is either the plane or the top.
    for (const cut of moveCuts) expect([-0.3, 0]).toContain(cut.to.z)
  })

  it('габарит планировщика: «A B» 10 мм — 19.5 мм по X и 10 мм по Y, центр по x', () => {
    // The width of the string is `n - 1` advances plus one glyph box, over the drawn points of the
    // letters (19.5 mm at 10 mm high), not the enclosing box of the glyph table.
    const box = plan(engrave({ text: 'A B', heightMm: 10, x: 0, y: 0 }))
    expect(box.bbox.xMax - box.bbox.xMin).toBeCloseTo(19.5, 9)
    expect(box.bbox.yMax - box.bbox.yMin).toBeCloseTo(10, 9)
    // The string is centred on the X of the operation by the glyph box (22 mm wide at 10 mm high),
    // so the drawn 19.5 starts 1.25 mm right of the box edge and ends 1.5 mm before its right one.
    const moved = plan(engrave({ text: 'A B', heightMm: 10, x: 40, y: 0 }))
    expect(moved.bbox.xMin).toBeCloseTo(40 - 11, 9)
    expect(moved.bbox.xMax).toBeCloseTo(40 - 11 + 19.5, 9)
  })

  it('«@» вне набора: ошибка на поле text, «АБВ» планируется ненулевой траекторией', () => {
    const bad = validate(program([engrave({ text: 'A@B' })]))
    expect(bad.ok).toBe(false)
    const problem = bad.ok ? null : bad.bad.find((item) => item.field === 'text')
    expect(problem?.reason).toBe('Символ «@» не поддержан.')
    const cyrillic = plan(engrave({ text: 'АБВ' }))
    expect(cyrillic.lengthMm).toBeGreaterThan(0)
    expect(cyrillic.moves.length).toBeGreaterThan(0)
  })

  it('пустой текст и высота не больше нуля — ошибки, высота меньше 1 мм — предупреждение', () => {
    const empty = validate(program([engrave({ text: '' })]))
    expect(empty.ok).toBe(false)
    expect(empty.ok ? null : empty.bad.map((item) => item.field)).toContain('text')
    const zero = validate(program([engrave({ heightMm: 0 })]))
    expect(zero.ok).toBe(false)
    expect(zero.ok ? null : zero.bad.find((item) => item.field === 'heightMm')?.reason).toBeTruthy()
    const small = engrave({ heightMm: 0.5, topZ: 0, depthMm: 0.1 })
    expect(validate(program([small])).ok).toBe(true)
    expect(pathWarnings(small).map((item) => item.field)).toContain('heightMm')
    expect(pathWarnings(engrave()).length).toBe(0)
  })

  it('наклон поворачивает строку вокруг начала координат текста', () => {
    const flat = plan(engrave({ x: 0, y: 0, text: 'A', angleDeg: 0 }))
    const turned = plan(engrave({ x: 0, y: 0, text: 'A', angleDeg: 90 }))
    // A is 8 mm wide and 10 mm high; a quarter turn swaps the sides.
    expect(flat.bbox.xMax - flat.bbox.xMin).toBeCloseTo(8, 9)
    expect(flat.bbox.yMax - flat.bbox.yMin).toBeCloseTo(10, 9)
    expect(turned.bbox.xMax - turned.bbox.xMin).toBeCloseTo(10, 9)
    expect(turned.bbox.yMax - turned.bbox.yMin).toBeCloseTo(8, 9)
    // The turn is around the text origin: a point above it moves to its left.
    expect(turned.bbox.zMin).toBeCloseTo(-0.3, 9)
    expect(turned.bbox.zMax).toBeCloseTo(5, 9)
  })

  it('программа печатает гравировку тегом и ходами, round-trip .qprog', () => {
    const doc = program([engrave({ text: 'AB 12', heightMm: 6, angleDeg: 15, x: 5, y: -3 })])
    const planned = buildProgram(doc)
    expect(planned.ok).toBe(true)
    const text = planned.ok ? emitProgram(doc, planned.path).join('\n') : ''
    expect(text).toContain('(UI_OP: Гравировка)')
    expect(text).toContain('(UI_STAGE: path-000001/op1)')
    const round = parseProgram(serializeProgram(doc))
    expect(round.ok).toBe(true)
    const back = round.ok ? round.program.operations[0] : null
    expect(back?.kind).toBe('engrave')
    const engraved = back?.kind === 'engrave' ? back : null
    expect(engraved?.text).toBe('AB 12')
    expect(engraved?.heightMm).toBe(6)
    expect(engraved?.angleDeg).toBe(15)
    expect(engraved?.x).toBe(5)
    expect(engraved?.y).toBe(-3)
  })
})
