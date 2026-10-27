// The program runner must not turn a stale negative cursor into a finished part: that path
// incremented partCount without an M30 and looked like a completed cycle.
import { describe, expect, it } from 'vitest'
import { executeLine } from './cycle'
import { initialState } from './machine'

describe('cycle: negative program cursor', () => {
  it('stops in ALARM and does not count a part', () => {
    const s = executeLine({ ...initialState, run: 'RUN', partCount: 41 }, -1)

    expect(s.run).toBe('ALARM')
    expect(s.programEnd).toBe(false)
    expect(s.partCount).toBe(41)
    expect(s.alarms.some((a) => a.level === 'error' && a.text.includes('курсор'))).toBe(true)
    expect(s.message?.level).toBe('error')
  })

  it('still marks a real end of file for the caller', () => {
    const s = executeLine({ ...initialState, run: 'RUN' }, 9999)

    expect(s.programEnd).toBe(true)
    expect(s.run).toBe('RUN')
  })
})
