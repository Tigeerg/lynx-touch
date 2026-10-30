// The DXF reader (PLAN-QUICKPROG, 7.6, «Импорт DXF»): only the contours, in the coordinates of the
// file.
//
// The reader takes ASCII DXF and the entities `LINE`, `LWPOLYLINE`, `POLYLINE`/`VERTEX`, `ARC` and
// `CIRCLE`; every arc becomes a chain of chords within `chordTol` of the true circle. The layers and
// the blocks are not read: the entities of `ENTITIES` are the drawing. An unsupported entity is
// counted and warned about, not an error; no supported entity at all is the error.
import { describe, expect, it } from 'vitest'

import { CHORD_TOL_MM, dxfContours, type DxfContour } from './dxf-read'

/** A DXF text of one section: the header of the file, `ENTITIES`, and the closing `EOF`. */
const dxf = (...entities: string[]): string =>
  ['0', 'SECTION', '2', 'ENTITIES', ...entities, '0', 'ENDSEC', '0', 'EOF'].join('\n')

/** One entity as a list of `(group code, value)` pairs. */
const entity = (type: string, ...pairs: (string | number)[]): string => ['0', type, ...pairs.map(String)].join('\n')

const ok = (text: string, chordTol = CHORD_TOL_MM): DxfContour[] => {
  const result = dxfContours(text, chordTol)
  if (!result.ok) throw new Error(result.reason)
  return result.contours
}

const fail = (text: string): string => {
  const result = dxfContours(text, CHORD_TOL_MM)
  if (result.ok) throw new Error('the reader accepted the text')
  return result.reason
}

/** The largest distance of the points from the circle. */
const deviation = (points: DxfContour['points'], cx: number, cy: number, r: number): number =>
  Math.max(...points.map((point) => Math.abs(Math.hypot(point.x - cx, point.y - cy) - r)))

describe('dxfContours: LINE', () => {
  it('собирает две LINE в один открытый контур из двух отрезков', () => {
    const text = dxf(
      entity('LINE', 8, '0', 10, 0, 20, 0, 11, 10, 21, 0),
      entity('LINE', 8, '0', 10, 10, 20, 0, 11, 10, 21, 5),
    )
    const contours = ok(text)

    expect(contours).toHaveLength(1)
    expect(contours[0].closed).toBe(false)
    expect(contours[0].points.map(({ x, y }) => [x, y])).toEqual([
      [0, 0],
      [10, 0],
      [10, 5],
    ])
  })

  it('сливает отрезки с общим концом и не склеивает разрыв', () => {
    const text = dxf(
      entity('LINE', 10, 0, 20, 0, 11, 10, 21, 0),
      entity('LINE', 10, 10, 20, 0, 11, 10, 21, 10),
      entity('LINE', 10, 100, 20, 100, 11, 110, 21, 100),
    )
    const contours = ok(text)

    expect(contours).toHaveLength(2)
    expect(contours[0].points).toHaveLength(3)
    expect(contours[1].points.map(({ x, y }) => [x, y])).toEqual([
      [100, 100],
      [110, 100],
    ])
  })
})

describe('dxfContours: LWPOLYLINE', () => {
  it('читает замкнутый четырёхугольник по флагу 1', () => {
    const text = dxf(
      entity('LWPOLYLINE', 8, '0', 90, 4, 70, 1, 10, 0, 20, 0, 10, 20, 20, 0, 10, 20, 20, 20, 10, 0, 20, 20),
    )
    const contours = ok(text)

    expect(contours).toHaveLength(1)
    expect(contours[0].closed).toBe(true)
    expect(contours[0].points.map(({ x, y }) => [x, y])).toEqual([
      [0, 0],
      [20, 0],
      [20, 20],
      [0, 20],
    ])
  })

  it('разбивает полукруг bulge 1 на точки, отклонение не больше допуска', () => {
    // Two vertices 20 mm apart, bulge 1: a semicircle of radius 10 over the chord, counter-clockwise.
    const text = dxf(entity('LWPOLYLINE', 90, 2, 70, 0, 10, 0, 20, 0, 42, 1, 10, 20, 20, 0, 42, 0))
    const [contour] = ok(text)

    expect(contour.closed).toBe(false)
    // The run goes from (0, 0) to (20, 0) over one half of the circle with the centre (10, 0). A
    // positive bulge is the AutoCAD convention: the arc bulges to the right of the run, so its middle
    // stands at (10, -10) and not above the chord.
    const middle = contour.points[(contour.points.length - 1) / 2]
    expect(middle.x).toBeCloseTo(10, 6)
    expect(middle.y).toBeCloseTo(-10, 6)
    expect(deviation(contour.points, 10, 0, 10)).toBeLessThanOrEqual(CHORD_TOL_MM)
    expect(contour.points[contour.points.length - 1]).toMatchObject({ x: 20, y: 0 })
    // The semicircle of radius 10 at 0.1 mm: 11 chords, so 11 inner points and the two ends.
    expect(contour.points).toHaveLength(13)
  })
})

describe('dxfContours: ARC и CIRCLE', () => {
  it('четверть круга радиуса 10 при допуске 0.1 — 6 хорд', () => {
    const text = dxf(entity('ARC', 10, 0, 20, 0, 40, 10, 50, 0, 51, 90))
    const [contour] = ok(text)

    expect(contour.closed).toBe(false)
    expect(contour.points).toHaveLength(7)
    expect(contour.points[0].x).toBeCloseTo(10, 6)
    expect(contour.points[6].y).toBeCloseTo(10, 6)
    expect(deviation(contour.points, 0, 0, 10)).toBeLessThanOrEqual(CHORD_TOL_MM)
  })

  it('полный CIRCLE радиуса 10 при допуске 0.1 — 23 хорды и 23 точки', () => {
    const text = dxf(entity('CIRCLE', 10, 0, 20, 0, 40, 10))
    const [contour] = ok(text)

    expect(contour.closed).toBe(true)
    expect(contour.points).toHaveLength(23)
    expect(deviation(contour.points, 0, 0, 10)).toBeLessThanOrEqual(CHORD_TOL_MM)
  })

  it('хорда дуги сужается с допуском: 0.01 мм даёт больше точек', () => {
    const text = dxf(entity('ARC', 10, 0, 20, 0, 40, 10, 50, 0, 51, 90))
    const [fine] = ok(text, 0.01)

    expect(fine.points.length).toBeGreaterThan(7)
    expect(deviation(fine.points, 0, 0, 10)).toBeLessThanOrEqual(0.01)
  })
})

describe('dxfContours: POLYLINE', () => {
  it('читает старый стиль POLYLINE/VERTEX/SEQEND', () => {
    const text = dxf(
      entity('POLYLINE', 70, 1),
      entity('VERTEX', 10, 0, 20, 0),
      entity('VERTEX', 10, 10, 20, 0),
      entity('VERTEX', 10, 10, 20, 10),
      entity('SEQEND'),
    )
    const [contour] = ok(text)

    expect(contour.closed).toBe(true)
    expect(contour.points.map(({ x, y }) => [x, y])).toEqual([
      [0, 0],
      [10, 0],
      [10, 10],
    ])
  })
})

describe('dxfContours: ошибки и пропуски', () => {
  it('SPLINE и TEXT пропускаются, контуров нет — ошибка с числом пропущенных', () => {
    const text = dxf(entity('SPLINE', 10, 0, 20, 0), entity('TEXT', 1, 'ОТВЕРСТИЕ', 10, 0, 20, 0))
    const reason = fail(text)

    expect(reason).toContain('2')
    expect(reason).toContain('контур')
  })

  it('без поддержанных сущностей предупреждение перечисляет пропущенные виды', () => {
    const result = dxfContours(dxf(entity('SPLINE', 10, 0, 20, 0), entity('TEXT', 1, 'X')), CHORD_TOL_MM)

    expect(result.ok).toBe(false)
    if (result.ok) return
    expect(result.skipped).toEqual(['SPLINE', 'TEXT'])
  })

  it('нет секции ENTITIES — ошибка', () => {
    const text = ['0', 'SECTION', '2', 'HEADER', '9', '$EXTMIN', '10', 0, '0', 'ENDSEC', '0', 'EOF'].join('\n')

    expect(fail(text)).toContain('ENTITIES')
  })

  it('LINE рядом с SPLINE даёт контур и предупреждение о пропуске', () => {
    const text = dxf(
      entity('SPLINE', 10, 0, 20, 0),
      entity('LINE', 10, 0, 20, 0, 11, 10, 21, 0),
      entity('TEXT', 1, 'X'),
    )
    const result = dxfContours(text, CHORD_TOL_MM)

    expect(result.ok).toBe(true)
    if (!result.ok) return
    expect(result.contours).toHaveLength(1)
    expect(result.skipped).toEqual(['SPLINE', 'TEXT'])
  })

  it('непарная строка и нечисловая координата — понятная ошибка, не исключение', () => {
    expect(() => dxfContours('0\nSECTION\n2\nENTITIES\nLINE', CHORD_TOL_MM)).not.toThrow()
    expect(fail(dxf(entity('LINE', 10, 'abc', 20, 0, 11, 10, 21, 0)))).toContain('LINE')
  })
})
