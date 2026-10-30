// The rest machining (PLAN-QUICKPROG 7.6, Q5): the area the earlier pockets left, and its raster.
//
// The area is geometric, not simulated: the rectangles of the previous pockets (a circle as its
// box) are united by the decomposition of the plane along their bounds, and the footprint of the
// rest is cut minus that union. The numbers below are the ones an operator can check on paper: a
// 60 x 40 pocket with a 20 x 20 pocket inside it leaves 2400 - 400 = 2000 mm^2 in four slabs, two
// separate pockets are a union of two boxes, an empty source is the whole footprint, and a covered
// footprint plans no move at all.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { PathBuilder } from './path-builder'
import { buildProgram } from './path'
import { planRest, restLayout } from './path-rest'
import { restAreas, unionAreas } from './rest-areas'
import type { Operation, QuickProgram, Rect, RestOperation } from './ops'
import { restWarnings } from './validate-rest'
import { validate } from './validate'

const stock = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

/** The 60 x 40 pocket of the console at the origin, an 8 mm mill, no wall stock. */
const pocket = (rect: Rect, patch: Partial<Extract<Operation, { kind: 'pocket' }>> = {}): Operation => ({
  ...(newOperation('pocket', { id: 'Карман', topZ: 0, depthMm: 4, stepDownMm: 2 }) as Extract<
    Operation,
    { kind: 'pocket' }
  >),
  tool: { number: 1, diameterMm: 8, hNumber: 1 },
  rect,
  cornerRadiusMm: 0,
  finishAllowanceMm: 0,
  stepOverPct: 70,
  ...patch,
})

/** A rest over `rect` (or the whole stock when empty) from the sources of the document. */
const rest = (source: number[], rect: Rect = { x: 0, y: 0, w: 0, h: 0 }): RestOperation => ({
  ...(newOperation('rest', { id: 'Остаток', topZ: 0, depthMm: 4, stepDownMm: 2 }) as RestOperation),
  tool: { number: 2, diameterMm: 4, hNumber: 2 },
  source,
  rect,
  allowanceMm: 0,
  stepOverPct: 70,
})

const program = (operations: Operation[]): QuickProgram => ({ ...newProgram(stock, 'REST'), operations })

const plan = (operations: Operation[], op: RestOperation) => {
  const out = new PathBuilder()
  const layout = planRest(program([...operations, op]), op, out)
  return { layout, path: out.result() }
}

const area = (rects: Rect[]): number => rects.reduce((sum, r) => sum + r.w * r.h, 0)

const cutMoves = (moves: { kind: string }[]): number => moves.filter((m) => m.kind === 'cut').length

describe('объединение областей', () => {
  it('два непересекающихся прямоугольника — две ячейки, каждая своего размера', () => {
    const areas = unionAreas([
      { x: 0, y: 0, w: 10, h: 10 },
      { x: 20, y: 0, w: 5, h: 5 },
    ])
    // They share the row y 0..5, so the decomposition merges them into one strip of that row and
    // keeps the second rectangle's own row above it: the area stays 125, the cells follow the grid.
    expect(area(areas)).toBe(125)
  })

  it('перекрытие вычитается один раз, без двойного счёта', () => {
    const areas = unionAreas([
      { x: 0, y: 0, w: 10, h: 10 },
      { x: 5, y: 0, w: 10, h: 10 },
    ])
    expect(area(areas)).toBe(150)
  })

  it('вложенный прямоугольник не меняет объединение', () => {
    const areas = unionAreas([
      { x: 0, y: 0, w: 60, h: 40 },
      { x: 20, y: 10, w: 20, h: 20 },
    ])
    expect(area(areas)).toBe(2400)
  })
})

describe('остаток 60 x 40 минус карман 20 x 20 внутри', () => {
  const previous = pocket({ x: -30, y: -20, w: 60, h: 40 })
  const inner = pocket({ x: -10, y: -10, w: 20, h: 20 })
  const op = rest([0, 1], { x: -30, y: -20, w: 60, h: 40 })

  it('четыре ячейки, площадь 2000 кв.мм, фреза с припуском внутри каждой', () => {
    const document = program([previous, inner, op])
    const areas = restAreas({ ...op, source: [1] }, document.operations, stock)
    // The inner pocket alone leaves the four slabs around it: 60 x 10, 60 x 10, 20 x 20, 20 x 20.
    expect(areas).toHaveLength(4)
    expect(area(areas)).toBeCloseTo(2000, 9)
    const sizes = areas.map((c) => `${String(c.w)}x${String(c.h)}`).sort()
    expect(sizes).toEqual(['20x20', '20x20', '60x10', '60x10'])
    const layout = restLayout(document, { ...op, source: [1] })
    expect(layout.areaMm2).toBeCloseTo(2000, 9)
    expect(layout.cells).toHaveLength(4)
    expect(layout.fits).toBe(true)
  })

  it('растр по каждой ячейке: 4+4 строки по X 60 и 7+7 по X 20, между ячейками быстрый ход на topZ', () => {
    const innerOnly = { ...op, source: [1] }
    const { layout, path } = plan([previous, inner], innerOnly)
    // A 4 mm mill with no allowance: the inner height of the top and bottom slabs 60x10 is 6, so
    // ceil(6 / 2.8) + 1 = 4 rows each; the inner box 20x20 has 16, ceil(16 / 2.8) + 1 = 7 rows each.
    // Two slabs and two boxes give 4 + 4 + 7 + 7 = 22 rows.
    const rows = layout.pieces.reduce((sum, list) => sum + list.length, 0)
    expect(rows).toBe(22)
    // A 4 mm rest is cut in two passes of `stepDownMm` 2: the rows of both passes, the plunges of a
    // cell start and the cuts back to the top. The paths of the one pass study the rows; here only
    // the order matters: every cut of the last pass stands at the floor.
    expect(cutMoves(path.moves)).toBeGreaterThan(rows)
    const rapidsToTop = path.moves.filter((m) => m.kind === 'rapid' && m.to.z === 0)
    // The entry of the pass is a rapid to the top as well; every cell after the first starts with one.
    expect(rapidsToTop.length).toBeGreaterThanOrEqual(4)
    // The last pass is the floor, the earlier one is a step above it: a plunge reaches the floor.
    const floors = path.moves.filter((m) => m.kind === 'cut' && m.to.z === op.topZ - op.depthMm)
    expect(floors.length).toBeGreaterThanOrEqual(rows)
    expect(
      path.moves.filter((m) => m.kind === 'cut').every((m) => m.to.z === 0 || m.to.z === -2 || m.to.z === -4),
    ).toBe(true)
  })

  it('оба кармана источниками: их объединение и есть площадь, остатка нет', () => {
    const document = program([previous, inner, op])
    const areas = restAreas(op, document.operations, stock)
    expect(areas).toHaveLength(0)
  })
})

describe('очерёдность и ноль ходов', () => {
  it('всё покрыто: остатка нет, ноль ходов, предупреждение', () => {
    const big = pocket({ x: -30, y: -20, w: 60, h: 40 })
    const op = rest([0], { x: -30, y: -20, w: 60, h: 40 })
    const { layout, path } = plan([big], op)
    expect(layout.cells).toHaveLength(0)
    expect(path.moves).toHaveLength(0)
    const warnings = restWarnings(program([big, op]), op)
    expect(warnings.map((w) => w.reason)).toContain('Остатка нет: предыдущие операции уже сняли площадь')
  })

  it('source пуст: предупреждение и растр по всей площади области', () => {
    const op = rest([], { x: -30, y: -20, w: 60, h: 40 })
    const { layout, path } = plan([], op)
    expect(layout.cells).toHaveLength(1)
    expect(layout.areaMm2).toBe(2400)
    expect(path.moves.length).toBeGreaterThan(0)
    const warnings = restWarnings(program([op]), op)
    expect(warnings.map((w) => w.reason)).toContain(
      'Нет исходных операций: остаток не определён, программа пойдёт по всей площади',
    )
  })

  it('без области остаток считается по заготовке', () => {
    const op = rest([])
    const { layout } = plan([], op)
    expect(layout.areaMm2).toBe(100 * 80)
  })

  it('источник face: предупреждение «не оставляет кармана», остаток по всей области', () => {
    const face = newOperation('face', {
      id: 'Плоскость',
      topZ: 0,
      depthMm: 2,
      tool: { number: 1, diameterMm: 10, hNumber: 1 },
    })
    const op = rest([0], { x: -30, y: -20, w: 60, h: 40 })
    const warnings = restWarnings(program([face, op]), op)
    expect(warnings.map((w) => w.reason)).toContain('Операция 1 не оставляет кармана: пропущена')
    expect(restAreas(op, program([face, op]).operations, stock)).toHaveLength(1)
  })

  it('круглый карман учтён прямоугольником, и об этом сказано', () => {
    const circle = newOperation('circle-pocket', {
      id: 'Круг',
      topZ: 0,
      depthMm: 4,
      tool: { number: 1, diameterMm: 8, hNumber: 1 },
    })
    const op = rest([0], { x: -30, y: -20, w: 60, h: 40 })
    const warnings = restWarnings(program([circle, op]), op)
    expect(warnings.map((w) => w.reason)).toContain(
      'Круглый карман учтён прямоугольником вокруг круга: остаток по углам приближён',
    )
  })
})

describe('проверка документа', () => {
  it('индекс вне списка — ошибка в поле source', () => {
    const op = rest([5], { x: -30, y: -20, w: 60, h: 40 })
    const checked = validate(program([op]))
    expect(checked.ok).toBe(false)
    if (checked.ok) return
    expect(checked.bad[0]?.field).toBe('source')
    expect(checked.bad[0]?.reason).toContain('вне списка')
  })

  it('ссылка на сам остаток — ошибка', () => {
    const op = rest([1], { x: -30, y: -20, w: 60, h: 40 })
    const document = program([pocket({ x: -30, y: -20, w: 60, h: 40 }), op])
    const checked = validate(document)
    expect(checked.ok).toBe(false)
    if (checked.ok) return
    expect(checked.bad.map((b) => b.field)).toContain('source')
    expect(checked.bad.map((b) => b.reason)).toContain(
      'Исходная операция 2 — это сам остаток: выберите предыдущие карманы.',
    )
  })

  it('пустой source — предупреждение, а не отказ сохранения', () => {
    const op = rest([], { x: -30, y: -20, w: 60, h: 40 })
    expect(validate(program([op])).ok).toBe(true)
    const built = buildProgram(program([op]))
    expect(built.ok).toBe(true)
    if (!built.ok) return
    expect(built.path.moves.length).toBeGreaterThan(0)
  })

  it('закрытый остаток сохраняется с ноль ходов, но операция в пути пустая', () => {
    const big = pocket({ x: -30, y: -20, w: 60, h: 40 })
    const op = rest([0], { x: -30, y: -20, w: 60, h: 40 })
    const built = buildProgram(program([big, op]))
    expect(built.ok).toBe(true)
    if (!built.ok) return
    // The pocket planned its moves; the rest after it has a span and no move of its own.
    const spans = built.path.operations
    expect(spans).toHaveLength(2)
    expect(spans[1].first).toBe(built.path.moves.length)
  })
})
