// The CAM_ARRAY and CAM_ROTATE actions of the overlay (PLAN-QUICKPROG, 7.6): the copies are flattened
// into the list on accept, the 100-copy limit refuses without touching the document, and a kind that
// cannot be copied says why.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import type { CamState } from './cam'
import type { MachineState } from '../types'
import { newOperation } from './defaults'
import type { RectPocketOperation } from './ops'

const run = (s: MachineState, ...actions: Action[]): MachineState => actions.reduce(reducer, s)

const cam = (state: MachineState): CamState => {
  if (state.cam === null) throw new Error('the overlay is closed')
  return state.cam
}

/** An open overlay with one rectangular pocket. */
const withPocket = (patch: Partial<RectPocketOperation> = {}) => {
  const open = run(initialState, { type: 'CAM_OPEN' })
  const pocket = {
    ...newOperation('pocket', { id: 'K', topZ: 0, depthMm: 2, stepDownMm: 1 }),
    rect: { x: -10, y: -5, w: 20, h: 10 },
    ...patch,
  }
  return { ...open, cam: { ...cam(open), program: { ...cam(open).program, operations: [pocket] } } }
}

describe('CAM_ARRAY: сетка копий', () => {
  it('3x2 кармана — шесть операций, смещения точные, id уникальны', () => {
    const before = withPocket()
    const after = run(before, { type: 'CAM_ARRAY', index: 0, cols: 3, rows: 2, dx: 30, dy: -20 })
    const list = cam(after).program.operations
    expect(list).toHaveLength(6)
    const rects = list.map((op) => (op.kind === 'pocket' ? [op.rect.x, op.rect.y] : null))
    expect(rects).toEqual([
      [-10, -5],
      [20, -5],
      [50, -5],
      [-10, -25],
      [20, -25],
      [50, -25],
    ])
    expect(new Set(list.map((op) => op.id)).size).toBe(6)
    // The copies stand right after the source, not at the end of the list.
    expect(list[0].id).toBe('K')
    expect(list[1].id).toBe('K #2')
  })

  it('массив больше ста операций — отказ, документ не меняется', () => {
    const before = withPocket()
    const after = run(before, { type: 'CAM_ARRAY', index: 0, cols: 20, rows: 6, dx: 1, dy: 1 })
    expect(after.cam?.program).toBe(cam(before).program)
    expect(after.message?.text).toBe('Слишком много копий: больше 100')
  })

  it('Custom G Code не клонируется и говорит почему', () => {
    const open = run(initialState, { type: 'CAM_OPEN' })
    const custom = { ...newOperation('custom', { id: 'G', topZ: 0 }), gcode: ['G0 X0 Y0'] }
    const before = { ...open, cam: { ...cam(open), program: { ...cam(open).program, operations: [custom] } } }
    const after = run(before, { type: 'CAM_ARRAY', index: 0, cols: 2, rows: 2, dx: 10, dy: 10 })
    expect(after.cam?.program.operations).toHaveLength(1)
    expect(after.message?.text).toBe('Custom G Code не копируется: его строки — готовый код, а не геометрия.')
  })
})

describe('CAM_ROTATE: копии вокруг точки', () => {
  it('три копии на 90° вокруг начала дают четыре операции', () => {
    const before = withPocket({ rect: { x: 0, y: 0, w: 20, h: 10 } })
    const after = run(before, { type: 'CAM_ROTATE', index: 0, angleDeg: 90, cx: 0, cy: 0, copies: 3 })
    const list = cam(after).program.operations
    expect(list).toHaveLength(4)
    const rects = list.map((op) =>
      op.kind === 'pocket' ? [Math.round(op.rect.x) + 0, Math.round(op.rect.y) + 0] : null,
    )
    expect(rects).toEqual([
      [0, 0],
      [-10, 0],
      [-20, -10],
      [0, -20],
    ])
  })

  it('карман на 30° — отказ с причиной оператора', () => {
    const before = withPocket()
    const after = run(before, { type: 'CAM_ROTATE', index: 0, angleDeg: 30, cx: 0, cy: 0, copies: 3 })
    expect(after.cam?.program).toBe(cam(before).program)
    expect(after.message?.text).toBe('Поворот кармана — только кратно 90 градусам.')
  })
})
