// Проверка наработки инструмента: время копится, пока шпиндель вращается в цикле,
// и обнуляется вместе с ресурсом.
import { describe, expect, it } from 'vitest'
import { initialState, reducer, type Action } from './machine'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: Action[]) => actions.reduce(reducer, state)

describe('наработка инструмента', () => {
  it('растёт, пока шпиндель вращается в цикле', () => {
    const s = run({ ...initialState, run: 'RUN', spindleOn: true }, { type: 'TICK', dt: 5000 })

    expect(s.toolTime[1]).toBe(5000)
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
