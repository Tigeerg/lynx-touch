// Cursor arithmetic over empty and stale lists. `clamp(x, 0, -1)` returned -1: the alarm cursor
// stuck at -1 after "clear all", PROG_TO_CURSOR wrote line: -1, and CYCLE_START then read the
// program as undefined and took the panel down. These checks walk those paths through the
// reducer - the layer the operator meets.
import { describe, expect, it } from 'vitest'
import { clampIndex } from './clamp'
import { programLines } from './files'
import { initialState, reducer } from './machine'

const emptyProgram = {
  ...initialState,
  screen: 'prog' as const,
  files: initialState.files.map((f, i) => (i === initialState.activeFile ? { ...f, lines: [] } : f)),
}

describe('clampIndex', () => {
  it('keeps an empty list at 0 and moves nowhere', () => {
    expect(clampIndex(0, 1, 0)).toBe(0)
    expect(clampIndex(0, -1, 0)).toBe(0)
    expect(clampIndex(7, -1, 0)).toBe(0)
  })

  it('moves inside a list and stops at both ends', () => {
    expect(clampIndex(0, 1, 3)).toBe(1)
    expect(clampIndex(1, 1, 3)).toBe(2)
    expect(clampIndex(2, 1, 3)).toBe(2)
    expect(clampIndex(0, -1, 3)).toBe(0)
    expect(clampIndex(1, 0, 3)).toBe(1)
  })
})

describe('cursor over the cleared alarm list', () => {
  it('stays at 0 in both directions', () => {
    const onAlarm = { ...initialState, screen: 'alarm' as const }
    let s = reducer(onAlarm, { type: 'ALARM_ACK' })
    expect(s.alarms.filter((al) => al.active)).toHaveLength(1)
    s = reducer(s, { type: 'CURSOR', delta: 1 })
    s = reducer(s, { type: 'ALARM_ACK' })
    s = reducer(s, { type: 'ALARM_CLEAR_ALL' })
    expect(s.alarms).toHaveLength(0)
    expect(reducer(s, { type: 'CURSOR', delta: 1 }).cursor.alarm).toBe(0)
    expect(reducer(s, { type: 'CURSOR', delta: -1 }).cursor.alarm).toBe(0)
  })
})

describe('program start', () => {
  it('does not write a negative line on an empty program', () => {
    const moved = reducer(emptyProgram, { type: 'CURSOR', delta: 1 })
    expect(moved.cursor.prog).toBe(0)
    const at = reducer(moved, { type: 'PROG_TO_CURSOR' })
    expect(at.line).toBeGreaterThanOrEqual(0)
    expect(at.nextLine).toBeGreaterThanOrEqual(0)
    expect(() => reducer(at, { type: 'CYCLE_START' })).not.toThrow()
  })

  it('runs an empty program without throwing', () => {
    expect(() => reducer({ ...emptyProgram, run: 'RUN' }, { type: 'TICK', dt: 50 })).not.toThrow()
    expect(() => reducer({ ...emptyProgram, run: 'RUN' }, { type: 'CYCLE_START' })).not.toThrow()
  })

  it('keeps a stale cursor inside the program', () => {
    const s = { ...initialState, screen: 'prog' as const, cursor: { ...initialState.cursor, prog: 9999 } }
    const at = reducer(s, { type: 'PROG_TO_CURSOR' })
    expect(at.line).toBe(programLines(s).length - 1)
  })

  it('starts at block 0 when the cursor is off the front', () => {
    const s = { ...initialState, screen: 'prog' as const, cursor: { ...initialState.cursor, prog: -3 } }
    const at = reducer(s, { type: 'PROG_TO_CURSOR' })
    expect(at.line).toBe(0)
    expect(at.nextLine).toBe(0)
  })
})
