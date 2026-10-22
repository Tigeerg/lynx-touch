// The PLC scan: comparisons and the presets of timers and counters. Written against the defects
// the review found by probing: a fractional constant was read as a signal, a preset carried its
// unit across a coil kind change (a timer's 3000 ms became 3000 counter pulses), and an address
// the reference does not know read as zero.
import { describe, expect, it } from 'vitest'
import { initialLadder } from './ladder'
import { evaluateLadder } from './ladder-scan'
import { initialState } from './machine'
import type { LadderCoil, LadderContact, LadderRung, MachineState } from './types'

const contact = (addr: string): LadderContact => ({ addr, label: addr })

const withRungs = (rungs: LadderRung[]): MachineState => ({
  ...initialState,
  ladder: {
    ...initialLadder,
    schemes: [{ name: 'plc_test', title: 'Тест', rungs, modified: '' }],
    active: 0,
    draft: null,
  },
})

const rung = (contacts: LadderContact[], coil: LadderCoil): LadderRung => ({ title: 'ступень', contacts, coil })

/** One scan; the memory carries over into the next call, as the TICK does. */
function tick(s: MachineState, dt = 0) {
  const result = evaluateLadder(s, undefined, dt)
  return { result, s: { ...s, ladder: { ...s.ladder, mem: result.mem } } }
}

describe('ladder scan: comparisons', () => {
  const compare = (a: string, op: '<' | '>' | '=' | '<>' | '<=' | '>=', b: string): MachineState =>
    withRungs([rung([{ addr: 'CMP', label: `${a} ${op} ${b}`, cmp: { op, a, b } }], contact('Q0.0'))])

  it('reads a fractional constant as a number', () => {
    expect(evaluateLadder(compare('0.5', '>', '0.1')).rungs[0].out).toBe(true)
    expect(evaluateLadder(compare('0.1', '>', '0.5')).rungs[0].out).toBe(false)
  })

  it('compares fractional constants in both directions', () => {
    expect(evaluateLadder(compare('0.1', '<', '0.5')).rungs[0].out).toBe(true)
    expect(evaluateLadder(compare('0.5', '=', '0.5')).rungs[0].out).toBe(true)
    expect(evaluateLadder(compare('0.5', '=', '0.25')).rungs[0].out).toBe(false)
  })

  it('keeps integer constants working', () => {
    expect(evaluateLadder(compare('2', '>=', '2')).rungs[0].out).toBe(true)
    expect(evaluateLadder(compare('-1', '<', '0')).rungs[0].out).toBe(true)
  })
})

describe('ladder scan: presets', () => {
  it('reaches the timer preset in milliseconds', () => {
    const s = withRungs([rung([contact('I0.0')], { addr: 'T0', label: 'Таймер', kind: 'ton', presetMs: 3000 })])
    const at1s = tick(s, 1000)
    expect(at1s.result.rungs[0].on).toBe(false)
    const at3s = tick(at1s.s, 2000)
    expect(at3s.result.rungs[0].on).toBe(true)
  })

  it('counts pulses up to the counter preset, not to the milliseconds of a timer', () => {
    // The coil is a counter that still carries a timer's 3000 ms: the scan must take the
    // counter's own default (10 pulses), not the stale milliseconds.
    const base = withRungs([rung([contact('M0.0')], { addr: 'C0', label: 'Счётчик', kind: 'ctu', presetMs: 3000 })])
    let s = tick({ ...base, spindleOn: false }).s
    for (let i = 0; i < 10; i++) {
      s = tick({ ...s, spindleOn: true }).s
      s = tick({ ...s, spindleOn: false }).s
    }
    expect(s.ladder.mem.counters.C0.acc).toBe(10)
    expect(s.ladder.mem.counters.C0.q).toBe(true)
  })

  it('reaches the counter preset in pulses', () => {
    const base = withRungs([rung([contact('M0.0')], { addr: 'C0', label: 'Счётчик', kind: 'ctu', presetCount: 3 })])
    let s = tick({ ...base, spindleOn: false }).s
    for (let i = 0; i < 3; i++) {
      s = tick({ ...s, spindleOn: true }).s
      s = tick({ ...s, spindleOn: false }).s
    }
    expect(s.ladder.mem.counters.C0.q).toBe(true)
  })
})
