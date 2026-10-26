// The spindle readout layer: rpm, "is cutting" and the demo load model live in one place,
// not three times on the screen components. The tests hold the numbers the operator sees.
import { describe, expect, it } from 'vitest'
import { initialState } from './machine'
import { isCutting, spindleLoad, spindleRpm } from './spindle'
import type { MachineState } from './types'

const machine = (patch: Partial<MachineState>): MachineState => ({ ...initialState, ...patch })

const cuttingMotion = { target: { X: 0, Y: 0, Z: -1, A: 0 }, rapid: false, feed: 500 }
const rapidMotion = { target: { X: 10, Y: 0, Z: 0, A: 0 }, rapid: true, feed: 5000 }

describe('spindleRpm', () => {
  it('выключенный шпиндель даёт 0, даже если заданы обороты и переопределение', () => {
    expect(spindleRpm(machine({ spindleOn: false, spindleSet: 1200, spindleOverride: 100 }))).toBe(0)
  })

  it('ESTOP гасит обороты при включённом шпинделе', () => {
    expect(spindleRpm(machine({ spindleOn: true, run: 'ESTOP', spindleSet: 1200 }))).toBe(0)
  })

  it('обороты — задание, умноженное на переопределение, с округлением', () => {
    expect(spindleRpm(machine({ spindleOn: true, spindleSet: 1200, spindleOverride: 50 }))).toBe(600)
    expect(spindleRpm(machine({ spindleOn: true, spindleSet: 1234, spindleOverride: 33 }))).toBe(407)
  })
})

describe('isCutting', () => {
  it('резание — это RUN с подачей, а не быстрым ходом', () => {
    expect(isCutting(machine({ run: 'RUN', motion: cuttingMotion }))).toBe(true)
  })

  it('быстрый ход, отсутствие движения и не-RUN не считаются резанием', () => {
    expect(isCutting(machine({ run: 'RUN', motion: rapidMotion }))).toBe(false)
    expect(isCutting(machine({ run: 'RUN', motion: null }))).toBe(false)
    expect(isCutting(machine({ run: 'IDLE', motion: cuttingMotion }))).toBe(false)
    expect(isCutting(machine({ run: 'HOLD', motion: cuttingMotion }))).toBe(false)
  })
})

describe('spindleLoad', () => {
  it('выключенный шпиндель не нагружен', () => {
    expect(spindleLoad(machine({ spindleOn: false, run: 'RUN', motion: cuttingMotion }))).toBe(0)
  })

  it('нулевые обороты при включённом шпинделе тоже дают 0', () => {
    const state = machine({ spindleOn: true, spindleSet: 0, run: 'RUN', motion: cuttingMotion, cycleTime: 400 })
    expect(spindleLoad(state)).toBe(0)
  })

  it('шпиндель включён без резания — базовая нагрузка 6', () => {
    const state = machine({ spindleOn: true, spindleSet: 1000, run: 'RUN', motion: rapidMotion, cycleTime: 400 })
    expect(spindleLoad(state)).toBe(6)
  })

  it('резание в начале цикла — базовая нагрузка 38', () => {
    const state = machine({ spindleOn: true, spindleSet: 1000, run: 'RUN', motion: cuttingMotion, cycleTime: 0 })
    expect(spindleLoad(state)).toBe(38)
  })

  it('детерминированный пример при cycleTime = 400', () => {
    const state = machine({ spindleOn: true, spindleSet: 1000, run: 'RUN', motion: cuttingMotion, cycleTime: 400 })
    // t = 1: 38 + sin(1) * 9 + sin(2.7) * 5 = 47.71 -> 48
    expect(spindleLoad(state)).toBe(48)
  })

  it('нагрузка всегда в 0..100 на длинном цикле', () => {
    for (let t = 0; t <= 4000; t += 37) {
      const state = machine({ spindleOn: true, spindleSet: 1000, run: 'RUN', motion: cuttingMotion, cycleTime: t })
      const load = spindleLoad(state)
      expect(load).toBeGreaterThanOrEqual(0)
      expect(load).toBeLessThanOrEqual(100)
    }
  })
})
