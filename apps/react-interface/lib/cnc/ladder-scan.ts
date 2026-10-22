// The PLC scan of the ladder subsystem: one pass over the rungs, with branch evaluation, timers,
// counters and latches. Kept apart from the ladder editor (ladder.ts) because the cycle (TICK)
// and the views need the scan alone.
import {
  coilKind,
  DEFAULT_PRESET_COUNT,
  DEFAULT_PRESET_MS,
  isCounterKind,
  isLatchKind,
  isTimerKind,
  NUMBER_RE,
  rungBranches,
  savedRungs,
} from './ladder'
import type { LadderContact, LadderMem, LadderRung, MachineState } from './types'

export interface LadderBranchResult {
  closed: boolean[]
  on: boolean
}

export interface LadderRungResult {
  closed: boolean[]
  branches: LadderBranchResult[]
  /** Coil input current (at least one branch closed). */
  out: boolean
  /** Coil output state: for a timer the Q bit, for Set/Reset the latch. */
  on: boolean
  /** Accumulated timer time, ms. */
  acc?: number
  /** Current counter value. */
  count?: number
}

export interface LadderResult {
  signals: Record<string, boolean>
  rungs: LadderRungResult[]
  mem: LadderMem
}

export function sameLadderMem(a: LadderMem, b: LadderMem) {
  const ta = Object.keys(a.timers)
  const la = Object.keys(a.latched)
  const ca = Object.keys(a.counters)
  return (
    ta.length === Object.keys(b.timers).length &&
    la.length === Object.keys(b.latched).length &&
    ca.length === Object.keys(b.counters).length &&
    ta.every((k) => b.timers[k]?.acc === a.timers[k].acc && b.timers[k]?.q === a.timers[k].q) &&
    la.every((k) => b.latched[k] === a.latched[k]) &&
    ca.every((k) => b.counters[k]?.acc === a.counters[k].acc && b.counters[k]?.q === a.counters[k].q)
  )
}

/** Shallow equality of two signal maps: TICK keeps the state reference when nothing changed. */
export function sameLadderSignals(a: Record<string, boolean>, b: Record<string, boolean>) {
  const keys = Object.keys(a)
  return keys.length === Object.keys(b).length && keys.every((k) => a[k] === b[k])
}

function resolveOperand(
  x: string,
  signals: Record<string, boolean>,
  timers: LadderMem['timers'],
  counters: LadderMem['counters'],
): number {
  const t = x.replace(/\s/g, '')
  if (NUMBER_RE.test(t)) return Number(t)
  if (/^T\d{1,2}$/.test(t)) return timers[t]?.acc ?? 0
  if (/^C\d{1,2}$/.test(t)) return counters[t]?.acc ?? 0
  return signals[t] ? 1 : 0
}

function compareValues(op: string, a: number, b: number): boolean {
  switch (op) {
    case '>':
      return a > b
    case '<':
      return a < b
    case '>=':
      return a >= b
    case '<=':
      return a <= b
    case '=':
      return a === b
    case '<>':
      return a !== b
    default:
      return false
  }
}

function evalContact(
  c: LadderContact,
  signals: Record<string, boolean>,
  timers: LadderMem['timers'],
  counters: LadderMem['counters'],
): boolean {
  if (c.cmp) {
    const a = resolveOperand(c.cmp.a, signals, timers, counters)
    const b = resolveOperand(c.cmp.b, signals, timers, counters)
    return compareValues(c.cmp.op, a, b)
  }
  return c.nc ? !signals[c.addr] : !!signals[c.addr]
}

/** One PLC scan. `dt` is the elapsed time in ms (0 - display only, without advancing timers). */
export function evaluateLadder(s: MachineState, ladder: LadderRung[] = savedRungs(s), dt = 0): LadderResult {
  const signals: Record<string, boolean> = {
    'I0.0': s.run !== 'ESTOP',
    'I0.1': true,
    'I0.2': true,
    'I0.3': true,
    'I0.4': true,
    'I0.5': !s.alarms.some((a) => a.code === 1104 && a.active),
    'M0.0': s.spindleOn,
    'M0.1': s.coolant,
    'M0.2': s.run === 'RUN',
    'M0.3': s.alarms.some((a) => a.active && a.level !== 'info') || s.run === 'ALARM' || s.run === 'ESTOP',
  }
  const prev = s.ladder.mem
  const latched: Record<string, boolean> = {}
  const timers: LadderMem['timers'] = {}
  const counters: LadderMem['counters'] = {}
  Object.assign(signals, prev.latched)
  for (const rung of ladder) {
    const k = coilKind(rung.coil)
    if (isTimerKind(k)) signals[rung.coil.addr] = prev.timers[rung.coil.addr]?.q ?? false
    else if (isCounterKind(k)) signals[rung.coil.addr] = prev.counters[rung.coil.addr]?.q ?? false
  }

  const rungs = ladder.map<LadderRungResult>((rung) => {
    const branches: LadderBranchResult[] = rungBranches(rung).map((contacts) => {
      const closed = contacts.map((c) => evalContact(c, signals, timers, counters))
      return { closed, on: closed.length > 0 && closed.every(Boolean) }
    })
    const closed = branches[0]?.closed ?? []
    const out = branches.some((b) => b.on)
    const { addr } = rung.coil
    const kind = coilKind(rung.coil)

    if (isTimerKind(kind)) {
      const preset = rung.coil.presetMs ?? DEFAULT_PRESET_MS
      const before = prev.timers[addr]?.acc ?? (kind === 'tof' ? preset : 0)
      let acc: number
      let q: boolean
      if (kind === 'ton') {
        acc = out ? Math.min(before + dt, preset) : 0
        q = out && acc >= preset
      } else {
        acc = out ? 0 : Math.min(before + dt, preset)
        q = acc < preset
      }
      timers[addr] = { acc, q }
      signals[addr] = q
      return { closed, branches, out, on: q, acc }
    }

    if (isCounterKind(kind)) {
      const preset = rung.coil.presetCount ?? DEFAULT_PRESET_COUNT
      const before = prev.counters[addr]
      const acc0 = before?.acc ?? (kind === 'ctd' ? preset : 0)
      const rising = out && !(before?.prev ?? false)
      const acc = kind === 'ctu' ? Math.min(acc0 + (rising ? 1 : 0), preset) : Math.max(acc0 - (rising ? 1 : 0), 0)
      const q = kind === 'ctu' ? acc >= preset : acc <= 0
      counters[addr] = { acc, q, prev: out }
      signals[addr] = q
      return { closed, branches, out, on: q, count: acc }
    }

    if (isLatchKind(kind)) {
      const value = out ? kind === 'set' : (latched[addr] ?? prev.latched[addr] ?? false)
      latched[addr] = value
      signals[addr] = value
      return { closed, branches, out, on: value }
    }

    signals[addr] = out
    return { closed, branches, out, on: out }
  })
  return { signals, rungs, mem: { timers, counters, latched } }
}
