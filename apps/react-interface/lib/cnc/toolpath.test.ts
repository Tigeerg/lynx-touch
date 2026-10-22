// Remaining-path check: it feeds the time estimate to the end of the program.
import { describe, expect, it } from 'vitest'
import { remainingLength } from './toolpath'

describe('остаток пути по кадрам', () => {
  it('считает длину перемещений от указанного кадра', () => {
    const lines = ['G0 X0 Y0', 'G1 X10', 'G1 Y10']

    expect(remainingLength(lines, 0)).toBeCloseTo(20)
    expect(remainingLength(lines, 2)).toBeCloseTo(10)
  })
})
