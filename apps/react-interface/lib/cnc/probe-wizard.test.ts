// Probe wizard subsystem: opening the probe screen, the step of the measurement cycle, the
// tool call, writing the result into an offset and the retry. The checks hold what the
// operator meets: the screen opens only in a stop, the measurement step requires the probe
// in the spindle, and the retry clears the result.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import { PROBE_TOOL } from './probe'
import { probeWizardReducer, type ProbeWizardAction } from './probe-wizard'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: ProbeWizardAction[]) =>
  actions.reduce((s, a) => probeWizardReducer(s, a).s, state)

describe('щуп: мастер измерения', () => {
  it('открытие уводит на экран смещений и начинает шаг с нуля', () => {
    const s = run(
      { ...initialState, mode: 'JOG', probe: { ...initialState.probe, step: 2 } },
      { type: 'PROBE_OPEN', open: true },
    )

    expect(s.probe.open).toBe(true)
    expect(s.screen).toBe('offset')
    expect(s.probe.step).toBe(0)
  })

  it('открытие закрыто, пока идёт программа', () => {
    const r = probeWizardReducer({ ...initialState, run: 'RUN' }, { type: 'PROBE_OPEN', open: true })

    expect(r.s.probe.open).toBe(false)
    expect(r.msg).toBe('Остановите программу')
  })

  it('в авто экран щупа заблокирован', () => {
    const r = probeWizardReducer({ ...initialState, mode: 'AUTO' }, { type: 'PROBE_OPEN', open: true })

    expect(r.s.probe.open).toBe(false)
    expect(r.msg).toContain('недоступен')
  })

  it('шаг измерения требует щуп в шпинделе', () => {
    const r = probeWizardReducer(
      { ...initialState, probe: { ...initialState.probe, step: 1 } },
      { type: 'PROBE_STEP', dir: 1 },
    )

    expect(r.msg).toContain('щуп T99')
  })

  it('вызов щупа ставит T99 и глушит шпиндель', () => {
    const s = run({ ...initialState, spindleOn: true }, { type: 'PROBE_CALL_TOOL' })

    expect(s.tool).toBe(PROBE_TOOL)
    expect(s.spindleOn).toBe(false)
  })

  it('повтор сбрасывает запись и возвращает на измерение', () => {
    const s = run(
      { ...initialState, probe: { ...initialState.probe, step: 4, written: true } },
      { type: 'PROBE_RETRY' },
    )

    expect(s.probe.written).toBe(false)
    expect(s.probe.step).toBe(3)
  })

  it('открытие доезжает до главного редьюсера', () => {
    const s = reducer({ ...initialState, mode: 'JOG' }, { type: 'PROBE_OPEN', open: true })

    expect(s.probe.open).toBe(true)
    expect(s.screen).toBe('offset')
  })
})
