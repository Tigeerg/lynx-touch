// The contour operation end to end (PLAN-QUICKPROG, 7.6): the type, the shape rule, the form, the
// `.qprog` round trip and the emitted file.
//
// A contour is a polyline of points in XY; the mill centre walks `D/2 + allowance` off it, on the
// material side or outside. A corner sharper than the mill cannot be cut without the shank rubbing
// the wall, and that is refused by the number of the vertex, not by "the contour".
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { emitProgram } from './emit'
import { formOperation, formProblems, newForm, valuesOf } from './form'
import type { ContourOperation, Operation, Point3, QuickProgram } from './ops'
import { turnRad } from './contour-offset'
import { buildProgram } from './path'
import { parseProgram, serializeProgram } from './serialize'
import { validate } from './validate'
import { viewerErrors } from './viewer-check'

const STOCK = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }
const p = (x: number, y: number): Point3 => ({ x, y, z: 0 })

/** The unit direction of a segment, for the angle the test asserts on. */
const unit = (from: Point3, to: Point3) => {
  const len = Math.hypot(to.x - from.x, to.y - from.y)
  return { x: (to.x - from.x) / len, y: (to.y - from.y) / len }
}

const contour = (patch: Partial<ContourOperation> = {}): ContourOperation => ({
  ...newOperation('contour', {
    id: 'Контур',
    topZ: 0,
    depthMm: 2,
    stepDownMm: 2,
    tool: { number: 1, diameterMm: 8, hNumber: 1 },
  }),
  points: [p(-20, -20), p(20, -20), p(20, 20), p(-20, 20)],
  closed: true,
  allowanceMm: 0,
  side: 'material',
  ...patch,
})

const program = (operations: Operation[]): QuickProgram => ({ ...newProgram(STOCK, 'CONTOUR'), operations })

const fields = (op: Operation): string[] => {
  const checked = validate(program([op]))
  return checked.ok ? [] : checked.bad.map((item) => item.field)
}

describe('contour type', () => {
  it('is a kind of the operation union with the common fields', () => {
    const op: Operation = contour()
    expect(op.kind).toBe('contour')
    expect(op.tool.diameterMm).toBe(8)
    expect(op.cutting.spindleRpm).toBeGreaterThan(0)
  })

  it('maps to the form values and back without losing the points', () => {
    const op = contour({ points: [p(0, 0), p(30, 0), p(30, 10)], closed: false, allowanceMm: 0.5, side: 'air' })
    const values = valuesOf(op)
    expect(values.points).toBe('0 0; 30 0; 30 10')
    expect(values.closed).toBe('off')
    expect(values.side).toBe('air')
    const back = formOperation({ op: 'contour', values, error: null }, op)
    expect(back).toMatchObject({ kind: 'contour', points: op.points, closed: false, allowanceMm: 0.5, side: 'air' })
  })

  it('reads a list with commas and blank pairs into the profile', () => {
    const form = newForm('contour', newProgram(STOCK))
    const values = { ...form.values, points: '0 0; 40,5 0;; 40.5 30;' }
    const op = formOperation({ ...form, values }, null)
    expect(op.kind === 'contour' && op.points.map(({ x, y }) => [x, y])).toEqual([
      [0, 0],
      [40.5, 0],
      [40.5, 30],
    ])
  })

  it('reports a broken point list at the points field', () => {
    const form = newForm('contour', newProgram(STOCK))
    const values = { ...form.values, points: '-20 -20; 20 zz' }
    const op = formOperation({ ...form, values }, null)
    expect(formProblems(newProgram(STOCK), values, op)).toEqual([
      { field: 'points', reason: 'Точка 2 задана не двумя числами X и Y.' },
    ])
  })
})

describe('contour rules', () => {
  it('refuses fewer than two points', () => {
    const op = contour({ points: [p(0, 0)] })
    const checked = validate(program([op]))
    expect(checked.ok ? [] : checked.bad).toEqual([
      { field: 'points', reason: 'Контур из одной точки: задайте хотя бы две.' },
    ])
  })

  it('refuses a 30 degree corner with an 8 mm mill and names the vertex', () => {
    // The apex at (0, 0), two rays 30° apart: the bisector reaches 4 mm at 4 / sin(15°) = 15.4 mm
    // from the vertex, the nearest wall is only 4 mm away, so the shank rubs.
    // The apex at (0, 0), the walk in along the left ray and out along the right one. The material
    // of the corner is the wedge below the apex; the mill body at 4 mm covers half a turn and cannot
    // enter a 30 degree wedge, so the vertex is refused.
    const a = 0.2617993877991494
    const far = 60
    const points = [p(-far * Math.cos(a), -far * Math.sin(a)), p(0, 0), p(far * Math.cos(a), -far * Math.sin(a))]
    expect(turnRad(unit(points[0], points[1]), unit(points[1], points[2]))).toBeCloseTo((Math.PI / 180) * 30, 9)
    const checked = validate(program([contour({ points, closed: false, side: 'material' })]))
    expect(checked.ok ? [] : checked.bad).toContainEqual({
      field: 'points',
      reason: 'Угол у точки 2 острее фрезы: радиус фрезы 4 мм не даёт пройти.',
    })
  })

  it('accepts a 90 degree corner with the same mill', () => {
    expect(fields(contour())).toEqual([])
  })

  it('refuses a contour wholly outside the stock', () => {
    const points = [p(100, 100), p(120, 100), p(120, 120)]
    const checked = validate(program([contour({ points })]))
    expect(checked.ok ? [] : checked.bad).toContainEqual({
      field: 'points',
      reason: 'Контур целиком вне заготовки: ни одна точка не попадает внутрь X -50..50, Y -40..40 мм.',
    })
  })

  it('refuses a negative allowance', () => {
    expect(fields(contour({ allowanceMm: -0.2 }))).toContain('points')
  })
})

describe('contour .qprog', () => {
  it('round-trips the operation through the file', () => {
    const doc = program([contour({ id: 'Профиль', points: [p(-20, -20), p(20, -20), p(20, 20)], closed: false })])
    const text = serializeProgram(doc)
    expect(text).toContain('op contour "Профиль"')
    expect(text).toContain('op.points -20 -20 20 -20 20 20')
    expect(text).toContain('op.closed off')
    expect(text).toContain('op.allowance 0')
    expect(text).toContain('op.side material')
    const read = parseProgram(text)
    expect(read.ok).toBe(true)
    if (!read.ok) return
    // The `.qprog` has no H row when it equals the tool number, as for every kind: the reader fills
    // zero, the console's "the tool number", and the profile keeps the plane of the document.
    expect(read.program.operations).toEqual(doc.operations.map((op) => ({ ...op, tool: { ...op.tool, hNumber: 0 } })))
  })

  it('names the missing key of a contour', () => {
    const text = serializeProgram(program([contour()])).replace('op.closed on\n', '')
    const read = parseProgram(text)
    expect(read.ok).toBe(false)
    if (read.ok) return
    expect(read.reason).toContain('op.closed')
  })
})

describe('contour reader', () => {
  const etl = (name: string): string => readFileSync(fileURLToPath(new URL(`./etl/${name}`, import.meta.url)), 'utf8')

  it('reads the reference contour document and writes it back byte for byte', () => {
    const text = etl('contour.qprog')
    const read = parseProgram(text)
    expect(read.ok).toBe(true)
    if (!read.ok) return
    expect(read.program.operations).toHaveLength(1)
    expect(serializeProgram(read.program)).toBe(text)
  })

  it('refuses an odd number of point words by the key', () => {
    const text = etl('contour.qprog').replace('op.points -20 -20 20 -20 20 20 -20 20', 'op.points -20 -20 20')
    const read = parseProgram(text)
    expect(read.ok).toBe(false)
    if (read.ok) return
    expect(read.reason).toContain("op.points ждёт пары 'x y'")
  })

  it('refuses a side word that is neither material nor air', () => {
    const text = etl('contour.qprog').replace('op.side material', 'op.side inside')
    const read = parseProgram(text)
    expect(read.ok).toBe(false)
    if (read.ok) return
    expect(read.reason).toContain('op.side ждёт material или air')
  })
})

describe('contour emit', () => {
  it('writes G1 moves on the offset path and passes the viewer check', () => {
    const doc = program([contour()])
    const built = buildProgram(doc)
    expect(built.ok).toBe(true)
    if (!built.ok) return
    const lines = emitProgram(doc, built.path)
    // The plunge at the first corner, then G1 through every joint: the centre square is 36 x 36.
    expect(lines).toContain('N65 G1 Z-2.000 F269')
    expect(lines).toContain('N70 X16.000 F1074')
    expect(lines).toContain('N75 Y16.000')
    expect(lines).toContain('N80 X-16.000')
    expect(lines).toContain('N85 Y-16.000')
    expect(viewerErrors(lines)).toEqual([])
  })
})
