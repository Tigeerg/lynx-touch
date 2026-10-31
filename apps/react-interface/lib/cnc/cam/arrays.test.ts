// The array and the rotation of an operation (PLAN-QUICKPROG, 7.6): the copies are flattened into
// the list on accept, and `cloneOperation` is the one switch that decides what a kind can copy.
import { describe, expect, it } from 'vitest'

import { arrayCopies, cloneOperation, MAX_COPIES, rotateOperation, rotatedCopies } from './copy-ops'
import { newOperation, newProgram } from './defaults'
import type { RectPocketOperation } from './ops'
import type { Operation } from './ops'

const pocket = (patch: Partial<RectPocketOperation> = {}): RectPocketOperation => ({
  ...newOperation('pocket', { id: 'K', topZ: 0, depthMm: 3, stepDownMm: 1 }),
  ...patch,
})

const circle = (patch: Partial<Extract<Operation, { kind: 'circle-pocket' }>> = {}): Operation => ({
  ...newOperation('circle-pocket', { id: 'C', topZ: 0 }),
  circle: { x: 10, y: 0, r: 6 },
  ...patch,
})

const custom = (): RectPocketOperation | Operation => ({
  ...newOperation('custom', { id: 'G', topZ: 0 }),
  gcode: ['G0 X0 Y0'],
})

const program = (operations: Operation[]) => ({ ...newProgram(), operations })

describe('cloneOperation: копия по смещению', () => {
  it('сдвигает прямоугольный карман и даёт уникальный id', () => {
    const source = pocket({ rect: { x: -10, y: -5, w: 20, h: 10 } })
    const copy = cloneOperation(source, 30, -12, 0, 0, 0, 2)
    expect(copy).not.toBeNull()
    if (copy === null || copy.kind !== 'pocket') throw new Error('expected a pocket')
    expect(copy.rect).toEqual({ x: 20, y: -17, w: 20, h: 10 })
    expect(copy.id).not.toBe(source.id)
    expect(copy.id).toBe('K #2')
  })

  it('сдвигает круглый карман, сетку сверления и точки контура', () => {
    const drilled = {
      ...newOperation('drill', { id: 'D', topZ: 0 }),
      pattern: 'grid' as const,
      x0: 0,
      y0: 0,
    }
    const contour = {
      ...newOperation('contour', { id: 'P', topZ: 0 }),
      points: [
        { x: 0, y: 0, z: 0 },
        { x: 10, y: 0, z: 0 },
      ],
    }
    const c = cloneOperation(contour, 5, 5, 0, 0, 0, 2)
    const d = cloneOperation(drilled, 5, 5, 0, 0, 0, 2)
    if (c === null || d === null || c.kind !== 'contour' || d.kind !== 'drill') throw new Error('unexpected kind')
    expect(c.points.map((point) => [point.x, point.y])).toEqual([
      [5, 5],
      [15, 5],
    ])
    expect([d.x0, d.y0]).toEqual([5, 5])
  })

  it('копия rest переносит прямоугольник, но не список source', () => {
    // A rest's `source` is the indexes of the operations it follows: they point into this document,
    // and a copy appended after them would name the wrong entries. The footprint moves, the sources
    // stay as they are (an empty list), so the copy is read as "the rest of the whole pocket".
    const source: Operation = {
      ...newOperation('rest', { id: 'Доработка', topZ: 0, depthMm: 3, stepDownMm: 1 }),
      rect: { x: -10, y: -5, w: 20, h: 10 },
      source: [0, 2],
    }
    const copy = cloneOperation(source, 30, -12, 0, 0, 0, 2)
    if (copy === null || copy.kind !== 'rest') throw new Error('expected a rest copy')
    expect(copy.rect).toEqual({ x: 20, y: -17, w: 20, h: 10 })
    expect(copy.source).toEqual(source.kind === 'rest' ? source.source : [])
    expect(copy.source).toEqual([0, 2])
    const turned = cloneOperation(source, 0, 0, 90, 0, 0, 2)
    if (turned === null || turned.kind !== 'rest') throw new Error('expected a rest copy')
    expect(turned.rect.x).toBeCloseTo(-5, 9)
    expect(turned.rect.y).toBeCloseTo(-10, 9)
    expect(turned.rect.w).toBeCloseTo(10, 9)
    expect(turned.rect.h).toBeCloseTo(20, 9)
    expect(turned.source).toEqual([0, 2])
  })

  it('не клонирует Custom G Code: поворот и сдвиг кода непонятен', () => {
    expect(cloneOperation(custom(), 10, 0, 0, 0, 0, 2)).toBeNull()
  })

  it('номер копии растёт: ` #3` из файла ` #2` даёт ` #4`', () => {
    const source = pocket({ id: 'K #2', enabled: false })
    const copy = cloneOperation(source, 1, 0, 0, 0, 0, 2)
    expect(copy?.id).toBe('K #3')
    expect(copy?.enabled).toBe(false)
  })
})

describe('rotateOperation: копия вокруг точки', () => {
  it('круг на 90° вокруг своего центра остаётся на месте', () => {
    const copy = rotateOperation(circle(), 90, 10, 0, 1)
    if (copy === null || copy.kind !== 'circle-pocket') throw new Error('unexpected kind')
    expect(copy.circle.x).toBeCloseTo(10, 9)
    expect(copy.circle.y).toBeCloseTo(0, 9)
    expect(copy.id).toBe('C #2')
  })

  it('круг не на центре поворота переезжает на радиус дуги', () => {
    const copy = rotateOperation(circle(), 90, 0, 0, 1)
    if (copy === null || copy.kind !== 'circle-pocket') throw new Error('unexpected kind')
    expect(copy.circle.x).toBeCloseTo(0, 9)
    expect(copy.circle.y).toBeCloseTo(10, 9)
  })

  it('прямоугольный карман на 90° вокруг начала: обрамляющий прямоугольник повёрнут', () => {
    // 20 x 10 from (0, 0) -> x 0..20, y 0..10; a quarter turn gives x -10..0, y 0..20.
    const source = pocket({ rect: { x: 0, y: 0, w: 20, h: 10 } })
    const copy = rotateOperation(source, 90, 0, 0, 1)
    if (copy === null || copy.kind !== 'pocket') throw new Error('unexpected kind')
    expect(copy.rect.x).toBeCloseTo(-10, 9)
    expect(copy.rect.y).toBeCloseTo(0, 9)
    expect(copy.rect.w).toBeCloseTo(10, 9)
    expect(copy.rect.h).toBeCloseTo(20, 9)
  })

  it('карман на 180° переносит прямоугольник через точку поворота', () => {
    const source = pocket({ rect: { x: 10, y: 10, w: 20, h: 10 } })
    const copy = rotateOperation(source, 180, 0, 0, 1)
    if (copy === null || copy.kind !== 'pocket') throw new Error('unexpected kind')
    expect(copy.rect.x).toBeCloseTo(-30, 9)
    expect(copy.rect.y).toBeCloseTo(-20, 9)
    expect(copy.rect.w).toBeCloseTo(20, 9)
    expect(copy.rect.h).toBeCloseTo(10, 9)
  })

  it('карман на 30° — отказ: поворот только кратно 90°', () => {
    expect(rotateOperation(pocket(), 30, 0, 0, 1)).toBeNull()
  })

  it('торцевание не поворачивается, паз и фаска поворачиваются точками', () => {
    expect(rotateOperation(newOperation('face', { id: 'F', topZ: 0 }), 90, 0, 0, 1)).toBeNull()
    const slot = rotateOperation(
      { ...newOperation('slot', { id: 'S', topZ: 0 }), to: { x: 20, y: 0, z: 0 } },
      90,
      0,
      0,
      1,
    )
    if (slot === null || slot.kind !== 'slot') throw new Error('unexpected kind')
    expect(slot.to.x).toBeCloseTo(0, 9)
    expect(slot.to.y).toBeCloseTo(20, 9)
  })

  it('метчик поворачивается точками: шаг резьбы не трогается', () => {
    const tap = { ...newOperation('tap', { id: 'T', topZ: 0 }), hole: { x: 10, y: 0, r: 3.4 }, pitchMm: 1.25 }
    const copy = rotateOperation(tap, 90, 0, 0, 1)
    if (copy === null || copy.kind !== 'tap') throw new Error('unexpected kind')
    expect(copy.hole.x).toBeCloseTo(0, 9)
    expect(copy.hole.y).toBeCloseTo(10, 9)
    expect(copy.pitchMm).toBe(1.25)
  })
})

describe('предел копий', () => {
  it('сто копий — предел записки', () => {
    expect(MAX_COPIES).toBe(100)
    expect(program([pocket()]).operations).toHaveLength(1)
  })
})

describe('плоский список: arrayCopies и rotatedCopies', () => {
  it('сетка 2x2 — источник первым, копии сразу за ним', () => {
    const result = arrayCopies(pocket(), { cols: 2, rows: 2, dx: 10, dy: 5 })
    if (!result.ok) throw new Error(result.reason)
    expect(result.operations.map((op) => op.id)).toEqual(['K', 'K #2', 'K #3', 'K #4'])
  })

  it('сетка, где источник с копиями больше ста, — отказ', () => {
    const result = arrayCopies(pocket(), { cols: 11, rows: 10, dx: 1, dy: 1 })
    expect(result).toEqual({ ok: false, reason: 'Слишком много копий: больше 100' })
  })

  it('сто операций с источником — предел, сто одна — отказ', () => {
    expect(arrayCopies(pocket(), { cols: 10, rows: 10, dx: 1, dy: 1 }).ok).toBe(true)
    expect(arrayCopies(pocket(), { cols: 101, rows: 1, dx: 1, dy: 0 }).ok).toBe(false)
  })

  it('карман на 30° в повороте — отказ до любых копий', () => {
    const result = rotatedCopies(pocket(), { angleDeg: 30, cx: 0, cy: 0, copies: 2 })
    expect(result).toEqual({ ok: false, reason: 'Поворот кармана — только кратно 90 градусам.' })
  })

  it('custom — отказ и в массиве, и в повороте', () => {
    const grid = arrayCopies(custom(), { cols: 2, rows: 1, dx: 1, dy: 0 })
    const turn = rotatedCopies(custom(), { angleDeg: 90, cx: 0, cy: 0, copies: 1 })
    expect(grid.ok).toBe(false)
    expect(turn.ok).toBe(false)
  })
})
