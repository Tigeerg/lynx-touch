// The break check performs a measurement, not just a message: a good tool passes, an exhausted
// life gives an alarm, and there is nothing to check with a probe in the spindle.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import type { MachineState } from './types'

const check = (over: Partial<MachineState> = {}) =>
  reducer({ ...initialState, toolClamped: true, ...over }, { type: 'BROKEN_TOOL_CHECK' })

describe('проверка слома через обмер', () => {
  it('исправный инструмент — в норме, без аварии', () => {
    const s = check()

    expect(s.run).toBe('IDLE')
    expect(s.message?.text).toContain('в норме')
    expect(s.alarms.some((a) => a.code === 4010 && a.active)).toBe(false)
  })

  it('исчерпанный ресурс — слом и авария', () => {
    const tools = initialState.tools.map((t) => (t.number === 1 ? { ...t, life: 0 } : t))
    const s = check({ tools })

    expect(s.run).toBe('ALARM')
    expect(s.message?.text).toContain('Слом')
    expect(s.alarms.some((a) => a.code === 4010 && a.active)).toBe(true)
  })

  it('щуп в шпинделе — проверять нечего', () => {
    const s = check({ tool: 99 })

    expect(s.message?.text).toBe('В шпинделе щуп T99 — проверять нечего')
  })
})
