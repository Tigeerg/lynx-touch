// Tool run-time check: the time accumulates while the spindle turns in a cycle, and is reset
// together with the life.
import { describe, expect, it } from 'vitest'
import { initialState, reducer, type Action } from './machine'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: Action[]) => actions.reduce(reducer, state)

describe('наработка инструмента', () => {
  it('растёт только на рабочем ходе, а не на холостом', () => {
    const cutting = {
      ...initialState,
      run: 'RUN' as const,
      spindleOn: true,
      motion: { target: initialState.machine, rapid: false, feed: 300 },
    }
    const rapid = { ...cutting, motion: { ...cutting.motion, rapid: true } }

    expect(run(cutting, { type: 'TICK', dt: 5000 }).toolTime[1]).toBe(5000)
    expect(run(rapid, { type: 'TICK', dt: 5000 }).toolTime[1] ?? 0).toBe(0)
  })

  it('не растёт при остановленном шпинделе', () => {
    const s = run({ ...initialState, run: 'RUN', spindleOn: false }, { type: 'TICK', dt: 5000 })

    expect(s.toolTime[1] ?? 0).toBe(0)
  })

  it('сброс ресурса обнуляет наработку инструмента', () => {
    const s = run(
      { ...initialState, toolTime: { 1: 12345 }, cursor: { ...initialState.cursor, tool: 0 } },
      { type: 'TOOL_RESET_LIFE' },
    )

    expect(s.toolTime[1]).toBe(0)
  })
})
