// The imported contours of a DXF file (PLAN-QUICKPROG, 7.6, «Импорт DXF»): the decision of what the
// overlay does with one contour.
//
// The `CAM_DXF` action carries the text of the file and the number of the contour the operator
// picked; the reducer reads the file once and puts the operation into the document, so the text
// itself is never stored. A file without a supported entity is a warning with the reason and the
// document stays as it was; a contour of one point is refused the same way.
import { describe, expect, it } from 'vitest'

import { dxfId, dxfOperation } from './dxf-contour'
import { pickDxfContour } from './dxf-pick'
import { dxfContours, readDxf } from './dxf-source'
import { newProgram } from './defaults'
import type { DxfContour } from './dxf-read'

const STOCK = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }
const program = () => newProgram(STOCK, 'DXF')

const contour = (points: [number, number][], closed = false): DxfContour => ({
  points: points.map(([x, y]) => ({ x, y })),
  closed,
})

describe('dxfOperation: контур в операцию', () => {
  it('делает contour с точками на верх заготовки, припуском 0 и стороной material', () => {
    const op = dxfOperation(
      contour(
        [
          [0, 0],
          [20, 0],
          [20, 10],
        ],
        true,
      ),
      program(),
    )

    expect(op).not.toBeNull()
    expect(op).toMatchObject({ kind: 'contour', closed: true, allowanceMm: 0, side: 'material', topZ: 0 })
    expect(op?.points.map(({ x, y, z }) => [x, y, z])).toEqual([
      [0, 0, 0],
      [20, 0, 0],
      [20, 10, 0],
    ])
    expect(op?.points).toHaveLength(3)
  })

  it('называет операцию числом точек контура', () => {
    expect(
      dxfId(
        contour([
          [0, 0],
          [5, 0],
          [5, 5],
        ]),
      ),
    ).toBe('DXF: 3 т.')
  })

  it('отказывает контуру из одной точки', () => {
    expect(dxfOperation(contour([[0, 0]]), program())).toBeNull()
  })
})

describe('readDxf: файл глазами оператора', () => {
  const dxf = (...entities: string[]): string =>
    ['0', 'SECTION', '2', 'ENTITIES', ...entities, '0', 'ENDSEC', '0', 'EOF'].join('\n')
  const entity = (type: string, ...pairs: (string | number)[]): string => ['0', type, ...pairs.map(String)].join('\n')

  it('отдаёт контуры и предупреждение о пропущенных сущностях', () => {
    const reading = readDxf(dxf(entity('LINE', 10, 0, 20, 0, 11, 10, 21, 0), entity('SPLINE', 10, 0, 20, 0)))

    expect(reading.ok).toBe(true)
    if (!reading.ok) return
    expect(reading.contours).toHaveLength(1)
    expect(reading.warning).toContain('SPLINE')
  })

  it('без контуров отдаёт причину и предупреждение о пропущенном', () => {
    const reading = readDxf(dxf(entity('TEXT', 1, 'X')))

    expect(reading.ok).toBe(false)
    if (reading.ok) return
    expect(reading.reason).toContain('TEXT')
    expect(reading.warning).toContain('TEXT')
  })

  it('пустой файл — ошибка', () => {
    expect(readDxf('').ok).toBe(false)
  })

  it('dxfContours читает файл в координатах DXF, без Z', () => {
    const result = dxfContours(dxf(entity('LWPOLYLINE', 90, 2, 70, 1, 10, 0, 20, 0, 10, 10, 20, 10)))

    expect(result.ok).toBe(true)
    if (!result.ok) return
    // Two vertices with no bulge: a segment, not a closed figure.
    expect(result.contours[0].closed).toBe(false)
    expect(result.contours[0].points).toEqual([
      { x: 0, y: 0 },
      { x: 10, y: 10 },
    ])
  })
})

describe('pickDxfContour: выбор контура по номеру', () => {
  const dxf = (...entities: string[]): string =>
    ['0', 'SECTION', '2', 'ENTITIES', ...entities, '0', 'ENDSEC', '0', 'EOF'].join('\n')
  const entity = (type: string, ...pairs: (string | number)[]): string => ['0', type, ...pairs.map(String)].join('\n')

  it('берёт контур по нулевому номеру и несёт предупреждение файла', () => {
    const picked = pickDxfContour(
      dxf(entity('LINE', 10, 0, 20, 0, 11, 10, 21, 0), entity('SPLINE', 10, 0, 20, 0)),
      0,
      program(),
    )

    expect(picked.ok).toBe(true)
    if (!picked.ok) return
    expect(picked.op.kind).toBe('contour')
    expect(picked.warning).toContain('SPLINE')
  })

  it('номер вне списка — причина с числом контуров', () => {
    const picked = pickDxfContour(dxf(entity('LINE', 10, 0, 20, 0, 11, 10, 21, 0)), 2, program())

    expect(picked.ok).toBe(false)
    if (picked.ok) return
    expect(picked.reason).toContain('1')
  })
})
