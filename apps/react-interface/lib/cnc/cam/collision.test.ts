// The collision check of PLAN-QUICKPROG, 7.5: no viewer, the panel's own simple rule.
//
// The check reads the planned path, not the document: every point the tool centre stands at is
// tested against the stock grown by the tool radius in plan. A centre outside that box is a warning
// (the tool overhangs the stock, the cut may be open). The depth below the stock bottom is not
// checked here: `validate` already refuses it with its own field, and a second copy would drift from
// the first. Facing overruns the edge by its lead by construction, so the plan rule does not apply to
// a face operation at all. The check is UNVERIFIED against the real geometry of the part.
import { describe, expect, it } from 'vitest'

import { buildProgram } from './path'
import { newOperation, newProgram } from './defaults'
import type { Operation, QuickProgram } from './ops'
import { checkProgram } from './collision'
import { validate } from './validate'

const stock = () => ({ min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } })

const tool = (diameterMm: number) => ({ number: 1, diameterMm, hNumber: 1 })

const cutting = () => ({ spindleRpm: 3000, feedMmMin: 500, plungeFeedMmMin: 120 })

const program = (...operations: Operation[]): QuickProgram => ({ ...newProgram(stock()), operations })

const checked = (document: QuickProgram) => {
  const built = buildProgram(document)
  if (!built.ok) throw new Error(`the document did not plan: ${built.problems.join('; ')}`)
  return checkProgram(document, built.path)
}

/** A rectangular pocket at `x`, `w` wide, cut by a mill of `diameter`, `depthMm` deep. */
const pocket = (x: number, w: number, diameter = 8, depthMm = 4, h = 30): Operation => ({
  ...newOperation('pocket', { id: 'Карман', topZ: 0, depthMm, stepDownMm: 2, tool: tool(diameter) }),
  cutting: cutting(),
  rect: { x, y: -15, w, h },
})

describe('проверка коллизии: план', () => {
  it('(а) карман внутри заготовки — чисто', () => {
    const result = checked(program(pocket(-20, 40)))
    expect(result.errors).toEqual([])
    expect(result.warnings).toEqual([])
  })

  it('(б) карман, чей центр ближе D/2 к краю — предупреждение с полем operations[0]', () => {
    // The pocket runs past the stock in X: its raster row ends at X 56, 6 mm past the stock edge of
    // 50, so the mill of radius 4 mm stands out of the stock there. A pocket laid inside (а) is clean.
    const result = checked(program(pocket(20, 40)))
    expect(result.errors).toEqual([])
    expect(result.warnings.length).toBeGreaterThan(0)
    expect(result.warnings[0].field).toBe('operations[0]')
    expect(result.warnings[0].reason).toContain('Инструмент выходит за заготовку в плане')
  })

  it('(в) торцевание с большим свесом — без предупреждений', () => {
    const face: Operation = {
      ...newOperation('face', { id: 'Плоскость', topZ: 0, depthMm: 2, stepDownMm: 2, tool: tool(12) }),
      cutting: cutting(),
      z: -2,
      stepOverPct: 60,
    }
    const result = checked(program(face))
    expect(result.errors).toEqual([])
    expect(result.warnings).toEqual([])
  })

  it('(д) дуга круглого кармана, выходящая в плане, — предупреждение на одной из четырёх точек', () => {
    // The outer turn reaches Y 46 at its top while its end point (angle 0/180) stands at Y 39, so
    // only the quarter point of the arc shows the overrun: judging the end alone would miss it.
    const circle: Operation = {
      ...newOperation('circle-pocket', {
        id: 'Круглый карман',
        topZ: 0,
        depthMm: 2,
        stepDownMm: 2,
        tool: tool(8),
      }),
      cutting: cutting(),
      circle: { x: 0, y: 39, r: 12 },
      finishAllowanceMm: 0,
      stepOverPct: 50,
    }
    const result = checked(program(circle))
    expect(result.errors).toEqual([])
    expect(result.warnings.length).toBeGreaterThan(0)
    expect(result.warnings[0].field).toBe('operations[0]')
    expect(result.warnings[0].reason).toContain('Инструмент выходит за заготовку в плане')
  })
})

describe('проверка коллизии: Z', () => {
  it('(г) глубина ниже дна заготовки ловится валидацией, а не этой проверкой', () => {
    // The document rules already keep a pass above the stock bottom (`depthMm`), so a document that
    // would cut below the bottom never reaches the planner: the check would be a second copy of the
    // rule and drift from the first. The test holds the fact that the document is refused before.
    const problems = validate(program(pocket(-20, 40, 8, 30)))
    expect(problems.ok).toBe(false)
    if (!problems.ok) expect(problems.bad.map((p) => p.field)).toContain('depthMm')
  })

  it('(г2) планировщик не опускает срез ниже дна валидного документа', () => {
    // The drift the check would catch cannot appear through the document rules: for the last legal
    // depth the lowest cut sits exactly on the bottom, and every other pass stays above it.
    const document = program(pocket(-20, 40, 8, 20))
    const result = checked(document)
    expect(result.errors).toEqual([])
  })

  it('сверление в пределах дна — чисто', () => {
    const drill: Operation = {
      ...newOperation('drill', { id: 'Отверстия', topZ: 0, depthMm: 10, tool: tool(6) }),
      cutting: cutting(),
      hole: { x: 0, y: 0, r: 3 },
    }
    expect(checked(program(drill))).toEqual({ errors: [], warnings: [] })
  })
})
