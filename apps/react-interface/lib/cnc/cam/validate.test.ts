// The safety rules of PLAN-QUICKPROG, section 8, as a list of problems bound to the form
// field. The texts are the ones of the console of `apps/quickprog`, so the panel and the demo
// tell the operator the same words.
import { describe, expect, it } from 'vitest'

import type {
  CirclePocketOperation,
  DrillOperation,
  FaceOperation,
  Operation,
  QuickProgram,
  RectPocketOperation,
  StockBox,
} from './ops'
import { newOperation } from './defaults'
import { defaultProgram, defaultStock } from './ops'
import { type Problem, tableRowFor, validate } from './validate'

const stock: StockBox = defaultStock(20, 0) // X -50..50, Y -40..40, Z -20..0

const program = (operations: Operation[], patch: Partial<QuickProgram> = {}): QuickProgram => ({
  ...defaultProgram(stock),
  operations,
  ...patch,
})

const face = (patch: Partial<FaceOperation> = {}): FaceOperation => ({
  ...newOperation('face', { id: 'f', topZ: 0, depthMm: 2, stepDownMm: 2 }),
  ...patch,
})

const pocket = (patch: Partial<RectPocketOperation> = {}): RectPocketOperation => ({
  ...newOperation('pocket', { id: 'p', topZ: 0, depthMm: 5, stepDownMm: 2.5 }),
  rect: { x: 0, y: 0, w: 60, h: 40 },
  ...patch,
})

const circlePocket = (patch: Partial<CirclePocketOperation> = {}): CirclePocketOperation => ({
  ...newOperation('circle-pocket', { id: 'c', topZ: 0, depthMm: 5, stepDownMm: 2.5 }),
  circle: { x: 0, y: 0, r: 20 },
  ...patch,
})

const drill = (patch: Partial<DrillOperation> = {}): DrillOperation => ({
  ...newOperation('drill', { id: 'd', topZ: 0, depthMm: 5, stepDownMm: 0 }),
  ...patch,
})

const fields = (problems: Problem[]): string[] => problems.map((p) => p.field)

describe('validate', () => {
  it('accepts a program that the demo accepts', () => {
    expect(validate(program([face(), pocket(), circlePocket(), drill()]))).toEqual({ ok: true })
  })

  it('rejects an empty list and a bad WCS', () => {
    expect(validate(program([]))).toEqual({
      ok: false,
      bad: [{ field: 'operations', reason: 'В программе нет ни одной операции.' }],
    })
    const badWcs = validate(program([face()], { wcs: 'G56' as QuickProgram['wcs'] }))
    expect(badWcs).toEqual({ ok: true })
    const missing = validate(program([face()], { wcs: 'G59.1' as QuickProgram['wcs'] }))
    expect(missing).toEqual({
      ok: false,
      bad: [{ field: 'wcs', reason: 'Смещение G59.1 не найдено: выберите G54..G59.' }],
    })
  })

  it('accepts every WCS of G54..G59', () => {
    for (const wcs of ['G54', 'G55', 'G56', 'G57', 'G58', 'G59'] as const) {
      expect(validate(program([face()], { wcs }))).toEqual({ ok: true })
    }
  })
})

describe('tool and cutting rules', () => {
  it('names the tool number, a negative H and a zero or oversized diameter', () => {
    expect(fields(problem(program([face({ tool: { number: 0, diameterMm: 10, hNumber: 0 } })])))).toContain(
      'tool.number',
    )
    expect(problem(program([face({ tool: { number: 1, diameterMm: 10, hNumber: -2 } })]))).toEqual([
      { field: 'tool.hNumber', reason: 'Номер коррекции H -2 отрицательный: в таблице коррекций нет такого номера.' },
    ])
    expect(problem(program([face({ tool: { number: 1, diameterMm: 0, hNumber: 1 } })]))).toEqual([
      { field: 'tool.diameterMm', reason: 'Диаметр инструмента должен быть больше нуля.' },
    ])
    expect(fields(problem(program([face({ tool: { number: 1, diameterMm: 250, hNumber: 1 } })])))).toContain(
      'tool.diameterMm',
    )
  })

  it('keeps the spindle, the feed and the plunge inside the machine and the table', () => {
    expect(problem(program([face({ cutting: { spindleRpm: 0, feedMmMin: 800, plungeFeedMmMin: 200 } })]))).toEqual([
      { field: 'cutting.spindleRpm', reason: 'Обороты шпинделя должны быть больше нуля.' },
    ])
    expect(
      fields(problem(program([face({ cutting: { spindleRpm: 30000, feedMmMin: 800, plungeFeedMmMin: 0 } })]))),
    ).toEqual(['cutting.spindleRpm', 'cutting.plungeFeedMmMin'])
    expect(problem(program([face({ cutting: { spindleRpm: 4775, feedMmMin: 20000, plungeFeedMmMin: 1 } })]))).toEqual([
      {
        field: 'cutting.feedMmMin',
        reason: 'Подача 20000 мм/мин выше максимума станка 15000 мм/мин.',
      },
    ])
    // More than three times the table row is called out with the source line.
    const tooFast = problem(program([face({ cutting: { spindleRpm: 4775, feedMmMin: 3000, plungeFeedMmMin: 200 } })]))
    expect(tooFast).toEqual([
      {
        field: 'cutting.feedMmMin',
        reason: 'Подача 3000 мм/мин втрое выше табличной (таблица: алюминий, ф10: 860). Проверьте режим.',
      },
    ])
    expect(problem(program([face({ cutting: { spindleRpm: 4775, feedMmMin: 500, plungeFeedMmMin: 600 } })]))).toEqual([
      { field: 'cutting.plungeFeedMmMin', reason: 'Подача врезания не должна быть выше рабочей подачи.' },
    ])
  })

  it('does not apply the plunge rule to a drill, which has one feed', () => {
    const op = drill({ cutting: { spindleRpm: 2122, feedMmMin: 255, plungeFeedMmMin: 0 } })
    expect(validate(program([op]))).toEqual({ ok: true })
  })
})

describe('depth, step and safe Z', () => {
  it('rejects a depth below the stock bottom', () => {
    // Two rules fire: the depth over the whole stock and the pass bottom under its bottom.
    const deep = problem(program([face({ depthMm: 25 })]))
    expect(fields(deep)).toEqual(['depthMm', 'depthMm'])
    expect(deep[0].reason).toBe('Глубина 25 мм ниже дна заготовки: снимается не более 20 мм.')
    expect(deep[1].reason).toBe('Дно прохода -25 мм ниже дна заготовки -20 мм: уменьшите глубину.')
    expect(problem(program([face({ depthMm: 0 })]))).toEqual([
      { field: 'depthMm', reason: 'Глубина обработки должна быть больше нуля.' },
    ])
  })

  it('rejects a zero step for a mill and a step over the depth', () => {
    expect(problem(program([pocket({ stepDownMm: 0 })]))).toEqual([
      { field: 'stepDownMm', reason: 'Шаг врезания должен быть больше нуля: иначе проходов не будет.' },
    ])
    expect(fields(problem(program([pocket({ stepDownMm: 8 })])))).toContain('stepDownMm')
  })

  it('treats a zero step of a drill as unused but a negative step as a mistake', () => {
    expect(validate(program([drill({ stepDownMm: 0 })]))).toEqual({ ok: true })
    expect(fields(problem(program([drill({ stepDownMm: -1 })])))).toContain('stepDownMm')
  })

  it('rejects a safe Z at or below the stock top', () => {
    // The stock top of the case frame is 0; an equal safe Z is still not above it.
    expect(problem(program([face()], { safeZ: 0 }))).toEqual([
      { field: 'safeZ', reason: 'Безопасный Z 0 мм не выше верха заготовки 0 мм.' },
    ])
  })

  it('rejects a G64 tolerance that is not a small positive number', () => {
    expect(fields(problem(program([face()], { g64P: 0 })))).toEqual(['g64P'])
    expect(fields(problem(program([face()], { g64Q: 2 })))).toEqual(['g64Q'])
  })
})

describe('shape rules', () => {
  it('rejects a mill wider than the stock and a plane above the material', () => {
    expect(fields(problem(program([face({ tool: { number: 1, diameterMm: 100, hNumber: 1 } })])))).toContain('geometry')
    expect(fields(problem(program([face({ z: 1 })])))).toContain('geometry')
    expect(fields(problem(program([face({ stepOverPct: 10 })])))).toContain('geometry')
  })

  it('rejects the raster step outside 10..100 % in a pocket', () => {
    const five = problem(program([pocket({ stepOverPct: 5 })]))
    expect(five).toEqual([
      {
        field: 'stepOver',
        reason: 'Шаг 5 % меньше 10 % диаметра: фреза будет тереть уже срезанное.',
      },
    ])
    expect(problem(program([pocket({ stepOverPct: 120 })]))).toEqual([
      {
        field: 'stepOver',
        reason: 'Шаг 120 % больше 100 % диаметра: между проходами останутся дыры.',
      },
    ])
  })

  it('rejects a mill and the allowance that do not fit a rectangular pocket', () => {
    // The 18 mm mill with a 1 mm wall allowance takes 10 mm from the wall of a 20 mm pocket:
    // exactly half the side, so the centre line is a point and nothing is cut.
    const op = pocket({
      rect: { x: 0, y: 0, w: 20, h: 20 },
      finishAllowanceMm: 1,
      tool: { number: 1, diameterMm: 18, hNumber: 1 },
    })
    expect(problem(program([op]))).toEqual([
      {
        field: 'geometry',
        reason:
          'Фреза ф18 мм и припуск 1 мм не влезают в карман 20x20 мм: нужно не больше 10 мм от стенки, а получается 10 мм. Возьмите фрезу поменьше.',
      },
    ])
    expect(fields(problem(program([pocket({ rect: { x: 0, y: 0, w: 0, h: 40 } })])))).toContain('geometry')
    expect(fields(problem(program([pocket({ cornerRadiusMm: -1 })])))).toContain('geometry')
    expect(fields(problem(program([pocket({ cornerRadiusMm: 25 })])))).toContain('geometry')
    expect(fields(problem(program([pocket({ finishAllowanceMm: -1 })])))).toContain('geometry')
  })

  it('rejects a mill that does not fit a circular pocket', () => {
    const op = circlePocket({ circle: { x: 0, y: 0, r: 5 }, tool: { number: 1, diameterMm: 10, hNumber: 1 } })
    expect(fields(problem(program([op])))).toEqual(['geometry'])
    expect(fields(problem(program([circlePocket({ circle: { x: 0, y: 0, r: 0 } })])))).toEqual(['geometry'])
  })

  it('rejects a peck deeper than the hole and a peck under 0.1 mm', () => {
    expect(problem(program([drill({ depthMm: 5, peckMm: 6 })]))).toEqual([
      {
        field: 'peck',
        reason: 'Шаг вывода стружки Q6 мм больше глубины отверстия 5 мм: такого цикла нет.',
      },
    ])
    expect(fields(problem(program([drill({ depthMm: 5, peckMm: 0.05 })])))).toEqual(['peck'])
    expect(validate(program([drill({ depthMm: 5, peckMm: 0 })]))).toEqual({ ok: true })
  })

  it('rejects a hole that stands outside the stock with its body', () => {
    const tool = { number: 1, diameterMm: 6, hNumber: 1 }
    const op = drill({ depthMm: 3, tool, hole: { x: 48, y: 0, r: 3 } })
    expect(problem(program([op]))).toEqual([
      {
        field: 'geometry',
        reason:
          'Отверстие (48, 0) вне заготовки: с инструментом ф6 мм оно заходит за края X -50..50, Y -40..40 мм. Сдвиньте отверстие или уменьшите фрезу.',
      },
    ])
  })

  it('rejects a grid point outside the stock and names the point', () => {
    // The third hole lands at X = 50 with a 6 mm drill: the body crosses the stock wall.
    const tool = { number: 1, diameterMm: 6, hNumber: 1 }
    const op = drill({ depthMm: 3, tool, pattern: 'grid', x0: -40, y0: 0, cols: 3, rows: 1, dx: 45, dy: 0 })
    const problems = problem(program([op]))
    expect(fields(problems)).toEqual(['geometry'])
    expect(problems[0].reason).toContain('(50, 0)')
    // Only the first hole outside is named.
    expect(problems).toHaveLength(1)
  })

  it('rejects a grid or ring of zero count', () => {
    const grid = drill({ pattern: 'grid', cols: 0, rows: 1 })
    expect(problem(program([grid]))).toEqual([
      { field: 'geometry', reason: 'Сетка отверстий: число по X и по Y должно быть больше нуля, а задано 0 и 1.' },
    ])
    expect(fields(problem(program([drill({ pattern: 'circle', count: 0 })])))).toEqual(['geometry'])
    expect(problem(program([drill({ pattern: 'circle', count: 4, hole: { x: 0, y: 0, r: 0 } })]))).toEqual([
      { field: 'geometry', reason: 'Круг отверстий: радиус окружности 0 мм должен быть больше нуля.' },
    ])
  })

  it('skips a disabled operation but still checks the program', () => {
    expect(validate(program([face({ enabled: false, depthMm: 99 })]))).toEqual({ ok: true })
  })
})

describe('helpers', () => {
  it('rates a drill by its body row and a mill by the flat row of its material', () => {
    expect(
      tableRowFor(drill({ material: 'steel', body: 'carbide', tool: { number: 1, diameterMm: 3.5, hNumber: 1 } })),
    ).toEqual({
      rpm: 6366,
      feedMmMin: 446,
      plungeFeedMmMin: 446,
      source: 'таблица: сверло карбид, сталь, ф3.5',
    })
    expect(tableRowFor(pocket({ material: 'steel' })).source).toBe('таблица: сталь, ф10')
  })
})

// The problems of a program, or an empty list; a valid program in the middle of a negative case
// would hide the rule the case is about.
function problem(p: QuickProgram): Problem[] {
  const result = validate(p)
  return result.ok ? [] : result.bad
}
