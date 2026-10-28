// Manual machine control subsystem: jog, overrides, the operator switches, the manual
// spindle and the MDI buffer. The checks hold the guards the operator meets: a move only in
// JOG and only in a stop, a soft limit stops the axis, dry run switches only in a stop, and
// the manual spindle refuses a probe in the spindle.
import { describe, expect, it } from 'vitest'
import { controlReducer, type ControlAction } from './control'
import { initialState, reducer } from './machine'
import { PROBE_TOOL } from './probe'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: ControlAction[]) =>
  actions.reduce((s, a) => controlReducer(s, a).s, state)

const atX = (x: number): MachineState => ({ ...initialState, machine: { X: x, Y: 0, Z: 0, A: 0 } })

describe('ручное управление: перемещение, тумблеры, MDI', () => {
  it('перемещение доступно только в ручном режиме', () => {
    const blocked = controlReducer({ ...atX(0), mode: 'AUTO' }, { type: 'JOG', dir: 1 })
    expect(blocked.msg).toContain('РУЧНОЙ')

    const s = run({ ...atX(0), mode: 'JOG', jogAxis: 'X', jogStep: 1 }, { type: 'JOG', dir: 1 })
    expect(s.machine.X).toBe(1)
  })

  it('программный предел останавливает ось', () => {
    const r = controlReducer({ ...atX(100000), mode: 'JOG', jogAxis: 'X', jogStep: 1 }, { type: 'JOG', dir: 1 })

    expect(r.s.machine.X).toBe(100000)
    expect(r.msg).toContain('предел')
  })

  it('тормоз оси A не даёт шагнуть', () => {
    const r = controlReducer({ ...atX(0), mode: 'JOG', jogAxis: 'A', aBrake: true }, { type: 'JOG', dir: 1 })

    expect(r.msg).toContain('Тормоз оси A')
  })

  it('шаг выбирается только в ручном режиме или маховике', () => {
    const blocked = controlReducer({ ...initialState, mode: 'AUTO' }, { type: 'SET_STEP', step: 0.1 })
    expect(blocked.msg).toContain('РУЧНОЙ')

    const s = run({ ...initialState, mode: 'JOG' }, { type: 'SET_STEP', step: 0.01 })
    expect(s.jogStep).toBe(0.01)
  })

  it('корректор подачи идёт шагом 10', () => {
    const s = run({ ...initialState, feedOverride: 100 }, { type: 'OVERRIDE', kind: 'feed', dir: 1 })

    expect(s.feedOverride).toBe(110)
  })

  it('корректор G0 берётся шагом по списку 5/25/50/100', () => {
    expect(
      run({ ...initialState, rapidOverride: 100 }, { type: 'OVERRIDE', kind: 'rapid', dir: 1 }).rapidOverride,
    ).toBe(100)
    expect(
      run({ ...initialState, rapidOverride: 100 }, { type: 'OVERRIDE', kind: 'rapid', dir: -1 }).rapidOverride,
    ).toBe(50)
  })

  it('холостой ход переключается только в останове', () => {
    const blocked = controlReducer({ ...initialState, dryRun: false, run: 'RUN' }, { type: 'TOGGLE', key: 'dryRun' })
    expect(blocked.s.dryRun).toBe(false)
    expect(blocked.msg).toContain('останове')

    const s = run({ ...initialState, dryRun: false }, { type: 'TOGGLE', key: 'dryRun' })
    expect(s.dryRun).toBe(true)
  })

  it('ручной пуск шпинделя запрещён в авто и при щупе', () => {
    const auto = controlReducer({ ...initialState, mode: 'AUTO' }, { type: 'SPINDLE_MANUAL' })
    expect(auto.s.spindleOn).toBe(false)

    const probe = controlReducer(
      { ...initialState, mode: 'JOG', tool: PROBE_TOOL, toolClamped: true },
      { type: 'SPINDLE_MANUAL' },
    )
    expect(probe.msg).toContain('щуп')
  })

  it('буфер MDI набирается в верхнем регистре и вызывается из истории', () => {
    const typed = run(initialState, { type: 'MDI_BUFFER', value: 'g0 x0' })
    expect(typed.mdi.buffer).toBe('G0 X0')

    const recalled = run(
      { ...initialState, mdi: { ...initialState.mdi, history: ['G90 G54'] } },
      { type: 'MDI_RECALL', index: 0 },
    )
    expect(recalled.mdi.buffer).toBe('G90 G54')
  })

  it('действие доезжает до главного редьюсера', () => {
    const s = reducer({ ...atX(0), mode: 'JOG', jogAxis: 'Y', jogStep: 1 }, { type: 'JOG', dir: 1 })

    expect(s.machine.Y).toBe(1)
  })
})
