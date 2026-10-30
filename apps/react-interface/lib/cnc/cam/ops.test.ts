// The CAM document types: the discriminated union narrows by `kind`, the common fields are
// shared, and the defaults of a new program and a new operation match PLAN-QUICKPROG, 7.3.
import { describe, expect, it } from 'vitest'

import { feedsForMill } from './feeds'
import { newOperation } from './defaults'
import {
  defaultProgram,
  drillGeometry,
  defaultStock,
  type FaceOperation,
  type Operation,
  type QuickProgram,
  type RectPocketOperation,
  type StockBox,
} from './ops'

const init = { id: 'x', topZ: 0 }
const stock = (): StockBox => defaultStock(10, 2)

const program = (operations: Operation[] = []): QuickProgram => ({
  ...defaultProgram(stock()),
  operations,
})

const face = (patch: Partial<FaceOperation> = {}): FaceOperation => ({
  ...newOperation('face', { id: 'op-1', topZ: 1, depthMm: 2, stepDownMm: 1 }),
  ...patch,
})

const pocket = (patch: Partial<RectPocketOperation> = {}): RectPocketOperation => ({
  ...newOperation('pocket', { id: 'op-2', topZ: 1, depthMm: 6, stepDownMm: 2 }),
  ...patch,
})

describe('operation union', () => {
  it('narrows the geometry by kind so only the fields of that kind are reachable', () => {
    const ops: Operation[] = [face(), pocket()]
    const described = ops.map((op) => {
      switch (op.kind) {
        case 'face':
          return `${op.kind}:${op.stepOverPct}:${op.z}`
        case 'pocket':
          return `${op.kind}:${op.rect.w}x${op.rect.h}:${op.stepOverPct}`
        case 'circle-pocket':
          return `${op.kind}:${op.circle.r}`
        case 'drill':
          return `${op.kind}:${op.pattern}`
      }
    })
    expect(described).toEqual(['face:70:-1', 'pocket:60x40:70'])
  })

  it('carries the common fields on every kind', () => {
    for (const op of [face(), pocket(), newOperation('circle-pocket', init), newOperation('drill', init)]) {
      expect(op.enabled).toBe(true)
      expect(op.tool.number).toBeGreaterThan(0)
      expect(op.cutting.spindleRpm).toBeGreaterThan(0)
      expect(op.material).toBe('aluminium')
      expect(op.coolant).toBe('flood')
      expect(typeof op.topZ).toBe('number')
      expect(typeof op.depthMm).toBe('number')
      expect(typeof op.stepDownMm).toBe('number')
      expect(op.notes).toBe('')
    }
  })
})

describe('drill layout', () => {
  it('takes the hole layout out of the flat drill fields', () => {
    const grid = newOperation('drill', { id: 'g', topZ: 0, tool: { number: 1, diameterMm: 6, hNumber: 1 } })
    const layout = { ...grid, pattern: 'grid' as const, cols: 2, dx: 10 }
    expect(drillGeometry(layout)).toEqual({ pattern: 'grid', x0: 0, y0: 0, cols: 2, rows: 1, dx: 10, dy: 0 })
    const hole = newOperation('drill', { id: 'h', topZ: 0, tool: { number: 1, diameterMm: 6, hNumber: 1 } })
    expect(drillGeometry(hole)).toEqual({ pattern: 'hole', hole: { x: 0, y: 0, r: 3 } })
  })
})

describe('defaults', () => {
  it('makes a stock box around its own top with the given size', () => {
    const box = defaultStock(20, 0)
    expect(box.max.z).toBe(0)
    expect(box.min.z).toBe(-20)
    expect(box.max.x - box.min.x).toBeCloseTo(100, 10)
    expect(box.max.y - box.min.y).toBeCloseTo(80, 10)
    expect(box.max.x).toBeCloseTo(50, 10)
    expect(box.min.y).toBeCloseTo(-40, 10)
  })

  it('makes a schema-1 program with the post defaults of 7.3', () => {
    const p = defaultProgram(stock())
    expect(p.schema).toBe(1)
    expect(p.units).toBe('mm')
    expect(p.wcs).toBe('G54')
    expect(p.g64P).toBe(0.01)
    expect(p.g64Q).toBe(0.01)
    expect(p.stopAfterOp).toBe(true)
    expect(p.safeZ).toBeCloseTo(p.stock.max.z + 5, 10)
    expect(p.operations).toEqual([])
    expect(p.name.length).toBeGreaterThan(0)
  })

  it('gives a face operation the plane at the pass bottom, 70 % raster and no axial step of a drill', () => {
    const op = newOperation('face', { id: 'f', topZ: 2, depthMm: 1.5, stepDownMm: 0.5 })
    expect(op.kind).toBe('face')
    if (op.kind !== 'face') throw new Error('kind')
    expect(op.z).toBeCloseTo(0.5, 10)
    expect(op.stepOverPct).toBe(70)
    const pocket = newOperation('pocket', init)
    expect(pocket.kind).toBe('pocket')
    if (pocket.kind === 'pocket') expect(pocket.stepOverPct).toBe(70)
    const drill = newOperation('drill', init)
    expect(drill.stepDownMm).toBe(0)
    expect(drill.kind === 'drill' && drill.pattern).toBe('hole')
    expect(drill.kind === 'drill' && drill.peckMm).toBe(0)
    expect(drill.kind === 'drill' && drill.body).toBe('hss')
  })

  it('rates the cutting data of a new operation from the feeds table and the diameter', () => {
    const op = newOperation('pocket', {
      id: 'p',
      topZ: 0,
      material: 'steel',
      tool: { number: 3, diameterMm: 8, hNumber: 3 },
    })
    const row = feedsForMill('steel', 8)
    expect(op.cutting.spindleRpm).toBe(row.rpm)
    expect(op.cutting.feedMmMin).toBe(row.feedMmMin)
    expect(op.cutting.plungeFeedMmMin).toBe(row.plungeFeedMmMin)
    expect(op.tool.hNumber).toBe(op.tool.number)
    expect(program([op]).operations).toHaveLength(1)
  })
})
