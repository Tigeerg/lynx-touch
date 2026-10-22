// Handwheel run subsystem check: the behaviour is fixed here, not by eye.
// The tests hold the machine coordinates, the block cursor and the message word - what the
// operator sees. The run goes only in AUTO and only with dry run on.
import { describe, expect, it } from 'vitest'
import { initialState, reducer, type Action } from './machine'

const auto = (over: Partial<typeof initialState> = {}) => ({ ...initialState, dryRun: true, ...over })
const step = (state: ReturnType<typeof auto>, dir: 1 | -1) => reducer(state, { type: 'MPG_HANDWHEEL', dir } as Action)

describe('прогон маховиком по траектории', () => {
  it('первый поворот вперёд выводит на первый кадр с координатами в машинных числах G54', () => {
    const s = step(auto(), 1)

    expect(s.line).toBe(3)
    expect(s.nextLine).toBe(4)
    expect(s.machine.X).toBeCloseTo(-120.5)
    expect(s.machine.Y).toBeCloseTo(35.25)
    expect(s.machine.Z).toBeCloseTo(-210.4)
    expect(s.message?.text).toBe('Прогон маховиком: кадр 4')
  })

  it('повторный поворот вперёд идёт по кадрам траектории', () => {
    const s = step(step(auto(), 1), 1)

    expect(s.line).toBe(5)
    expect(s.machine.Z).toBeCloseTo(-160.4)
  })

  it('поворот назад до первого кадра сообщает о начале траектории', () => {
    const s = step(auto(), -1)

    expect(s.line).toBe(0)
    expect(s.message?.text).toBe('Начало траектории')
  })

  it('за концом УП прогон вперёд упирается в конец траектории', () => {
    const s = step(auto({ line: 23 }), 1)

    expect(s.line).toBe(23)
    expect(s.message?.text).toBe('Траектория пройдена — конец УП')
  })

  it('без холостого хода прогон отклонён', () => {
    const s = reducer(initialState, { type: 'MPG_HANDWHEEL', dir: 1 } as Action)

    expect(s.message?.text).toBe('Включите холостой ход — по нему идёт прогон маховиком')
  })

  it('в ручном режиме прогон отклонён', () => {
    const s = step(auto({ mode: 'JOG' as const }), 1)

    expect(s.message?.text).toBe('Прогон маховиком — в режиме АВТО')
  })
})
