// Remaining-path check: it feeds the time estimate to the end of the program.
import { describe, expect, it } from 'vitest'
import { remainingLength, remainingTimeMs } from './toolpath'

describe('остаток пути по кадрам', () => {
  it('считает длину перемещений от указанного кадра', () => {
    const lines = ['G0 X0 Y0', 'G1 X10', 'G1 Y10']

    expect(remainingLength(lines, 0)).toBeCloseTo(20)
    expect(remainingLength(lines, 2)).toBeCloseTo(10)
  })
})

describe('остаток времени', () => {
  it('считает G1 по подаче, а G0 по быстрому ходу', () => {
    const lines = ['G1 X100 F1000', 'G0 X200', 'G1 X300 F1000']
    const ms = remainingTimeMs(lines, 0, 1000, 15000)

    expect(ms).toBeCloseTo((100 / 15000 + 100 / 1000) * 60_000)
  })
})
