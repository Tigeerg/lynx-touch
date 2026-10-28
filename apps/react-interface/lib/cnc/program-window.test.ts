// The visible slice of a long NC program: a real CAM file has tens of thousands of blocks, so the
// list draws only a window around the followed block instead of the whole file.
import { describe, expect, it } from 'vitest'
import { PROGRAM_WINDOW, programWindow } from './program-window'

describe('окно списка кадров', () => {
  it('короткая программа показывается целиком', () => {
    expect(programWindow(0, 10)).toEqual({ start: 0, end: 10 })
    expect(programWindow(5, PROGRAM_WINDOW)).toEqual({ start: 0, end: PROGRAM_WINDOW })
  })

  it('в начале окно прижато к первому кадру', () => {
    expect(programWindow(0, 1000)).toEqual({ start: 0, end: PROGRAM_WINDOW })
    expect(programWindow(3, 1000)).toEqual({ start: 0, end: PROGRAM_WINDOW })
  })

  it('в середине кадр стоит примерно по центру окна', () => {
    const { start, end } = programWindow(500, 1000)

    expect(start).toBe(500 - Math.floor(PROGRAM_WINDOW / 2))
    expect(end).toBe(start + PROGRAM_WINDOW)
    expect(start).toBeLessThanOrEqual(500)
    expect(end).toBeGreaterThan(500)
  })

  it('в конце окно не выходит за последний кадр', () => {
    expect(programWindow(999, 1000)).toEqual({ start: 1000 - PROGRAM_WINDOW, end: 1000 })
    expect(programWindow(5000, 1000)).toEqual({ start: 1000 - PROGRAM_WINDOW, end: 1000 })
  })

  it('пустая программа даёт пустое окно', () => {
    expect(programWindow(0, 0)).toEqual({ start: 0, end: 0 })
  })
})
