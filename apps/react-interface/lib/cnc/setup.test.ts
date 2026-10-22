// Setup subsystem: the offset and tool screens plus zeroing. The checks hold the contract
// the reducer shows to the operator: teach writes the machine value into the active offset,
// zeroing is allowed only in JOG, a tool call only in a stop, and the buffer edits step back.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import { setupReducer, type SetupAction } from './setup'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: SetupAction[]) => actions.reduce((s, a) => setupReducer(s, a).s, state)

const machine: MachineState['machine'] = { X: 12.5, Y: -3, Z: 4, A: 0 }

describe('наладка: смещения, инструмент, обнуление', () => {
  it('активной становится система под курсором', () => {
    const s = run({ ...initialState, cursor: { ...initialState.cursor, offset: 2 } }, { type: 'OFFSET_ACTIVATE' })

    expect(s.activeOffset).toBe(2)
  })

  it('привязка пишет машинную координату в смещение под курсором', () => {
    const s = run(
      { ...initialState, machine, cursor: { ...initialState.cursor, offset: 1 } },
      { type: 'OFFSET_TEACH', axis: 'X' },
    )

    expect(s.offsets[1].values.X).toBe(machine.X)
  })

  it('обнуление рабочих координат — только в режиме РУЧНОЙ', () => {
    const blocked = setupReducer({ ...initialState, mode: 'AUTO' }, { type: 'ZERO_WORK' })
    expect(blocked.msg).toContain('РУЧНОЙ')

    const done = run({ ...initialState, mode: 'JOG', machine, activeOffset: 0 }, { type: 'ZERO_WORK' })
    expect(done.offsets[0].values).toEqual(machine)
  })

  it('обнуление относительных координат берёт текущую позицию', () => {
    const s = run({ ...initialState, machine }, { type: 'ZERO_REL' })

    expect(s.relOrigin).toEqual(machine)
  })

  it('вызов инструмента ставит его в шпиндель только в останове', () => {
    const called = run({ ...initialState, mode: 'JOG' }, { type: 'TOOL_CALL' })
    expect(called.tool).toBe(initialState.tools[initialState.cursor.tool].number)

    const busy = setupReducer({ ...initialState, mode: 'JOG', run: 'RUN' }, { type: 'TOOL_CALL' })
    expect(busy.msg).toBe('Смена инструмента в режиме РУЧНОЙ / MDI')
  })

  it('измерение задаёт длину инструмента под курсором', () => {
    const s = run({ ...initialState, cursor: { ...initialState.cursor, tool: 0 } }, { type: 'TOOL_MEASURE' })

    expect(s.tools[0].length).toBeGreaterThan(0)
  })

  it('ресурс инструмента сбрасывается на 100 и наработка обнуляется', () => {
    const s = run(
      { ...initialState, cursor: { ...initialState.cursor, tool: 0 }, toolTime: { 1: 5000 } },
      { type: 'TOOL_RESET_LIFE' },
    )

    expect(s.tools[0].life).toBe(100)
    expect(s.toolTime[initialState.tools[0].number]).toBe(0)
  })

  it('буфер номера инструмента стирается по цифре', () => {
    const s = run({ ...initialState, toolBuffer: '12' }, { type: 'TOOL_BACK' })

    expect(s.toolBuffer).toBe('1')
  })

  it('настройка доезжает до главного редьюсера', () => {
    const s = reducer({ ...initialState, machine }, { type: 'ZERO_REL' })

    expect(s.relOrigin).toEqual(machine)
  })
})
