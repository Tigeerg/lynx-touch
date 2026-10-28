// Bounds of a big sequence: the toolpath of a real program has more points than a spread call can
// take, so the extent must be a plain pass.
import { describe, expect, it } from 'vitest'
import { extent } from './extent'

describe('границы последовательности', () => {
  it('находит минимум и максимум', () => {
    expect(extent([3, -1, 7, 0])).toEqual({ min: -1, max: 7 })
  })

  it('работает на очень длинной последовательности без переполнения стека', () => {
    const values = Array.from({ length: 200_000 }, (_, i) => i - 100_000)

    expect(extent(values)).toEqual({ min: -100_000, max: 99_999 })
  })

  it('пустая последовательность даёт бесконечные границы', () => {
    expect(extent([])).toEqual({ min: Infinity, max: -Infinity })
  })
})
