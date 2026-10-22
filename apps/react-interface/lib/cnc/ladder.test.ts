// The ladder editor through the reducer, the layer the operator meets. Written against the
// defects the review found by probing: a compare could silently read an address the scheme does
// not define as zero, a preset carried its unit across a coil kind change, and a stale scheme
// index or an empty scheme took the reducer down.
import { describe, expect, it } from 'vitest'
import { initialLadder, ladderCursor } from './ladder'
import { initialState, reducer } from './machine'
import type { LadderCoil, LadderContact, LadderRung, MachineState } from './types'

const contact = (addr: string): LadderContact => ({ addr, label: addr })
const rung = (contacts: LadderContact[], coil: LadderCoil): LadderRung => ({ title: 'ступень', contacts, coil })

const withLadder = (patchLadder: Partial<MachineState['ladder']>): MachineState => ({
  ...initialState,
  ladder: { ...initialLadder, ...patchLadder },
})

const withRungs = (rungs: LadderRung[]): MachineState =>
  withLadder({ schemes: [{ name: 'plc_test', title: 'Тест', rungs, modified: '' }], active: 0, draft: null })

/** Entering the edit, adding a compare block and typing the condition. */
function commitCompare(text: string) {
  let s = reducer(initialState, { type: 'LADDER_EDIT', on: true })
  s = reducer(s, { type: 'LADDER_ADD_COMPARE' })
  s = reducer(s, { type: 'LADDER_FORM_SET', index: 0, value: text })
  return reducer(s, { type: 'LADDER_FORM_COMMIT' })
}

describe('ladder editor: compare blocks', () => {
  it('refuses a compare against an address the scheme does not define', () => {
    const s = commitCompare('M7.3 > 1')
    expect(s.ladder.form).not.toBeNull()
    expect(s.message?.text).toContain('M7.3')
  })

  it('accepts a compare with a fractional constant', () => {
    const s = commitCompare('0.5 > 0.1')
    expect(s.ladder.form).toBeNull()
    const element = s.ladder.draft?.[0].contacts.find((c) => c.cmp)
    expect(element?.cmp).toEqual({ op: '>', a: '0.5', b: '0.1' })
  })

  it('accepts a compare against a coil the scheme defines', () => {
    const s = reducer(withRungs([rung([contact('I0.0')], { addr: 'M1.0', label: 'Флаг' })]), {
      type: 'LADDER_EDIT',
      on: true,
    })
    const withCompare = reducer(s, { type: 'LADDER_ADD_COMPARE' })
    const typed = reducer(withCompare, { type: 'LADDER_FORM_SET', index: 0, value: 'M1.0 >= 1' })
    const done = reducer(typed, { type: 'LADDER_FORM_COMMIT' })
    expect(done.ladder.form).toBeNull()
    expect(done.ladder.draft?.[0].contacts.some((c) => c.cmp?.a === 'M1.0')).toBe(true)
  })

  it('still refuses a malformed condition', () => {
    const s = commitCompare('ерунда')
    expect(s.ladder.form).not.toBeNull()
    expect(s.message?.text).toBeTruthy()
  })
})

describe('ladder editor: coil presets', () => {
  it('keeps milliseconds and pulses in their own fields when the kind changes', () => {
    const timer = rung([contact('I0.0')], { addr: 'T0', label: 'Таймер', kind: 'ton', presetMs: 3000 })
    const s = withLadder({ schemes: [{ name: 'plc_test', title: 'т', rungs: [timer], modified: '' }], draft: [timer] })
    // The cursor sits on the coil: the rung has one contact.
    const onCoil = { ...s, ladder: { ...s.ladder, col: 1, path: 0 } }
    let next = reducer(onCoil, { type: 'LADDER_TOGGLE_NC' })
    next = reducer({ ...next, ladder: { ...next.ladder, col: 1 } }, { type: 'LADDER_TOGGLE_NC' })
    const coil = next.ladder.draft?.[0].coil
    expect(coil?.kind).toBe('ctu')
    expect(coil?.presetMs).toBe(3000)
    expect(coil?.presetCount).toBeUndefined()
  })
})

describe('ladder editor: a new rung takes a free coil address', () => {
  it('does not reuse an address already on the scheme', () => {
    const used = rung([contact('I0.0')], { addr: 'M1.0', label: 'Занято' })
    const s = reducer(withRungs([used]), { type: 'LADDER_EDIT', on: true })
    const added = reducer(s, { type: 'LADDER_ADD_RUNG' })
    const addresses = (added.ladder.draft ?? []).map((r) => r.coil.addr)

    expect(addresses).toContain('M1.0')
    expect(new Set(addresses).size).toBe(addresses.length)
  })
})

describe('ladder editor: scheme list guards', () => {
  it.each([{ type: 'LADDER_LOAD' }, { type: 'LADDER_COPY' }, { type: 'LADDER_RENAME' }] as const)(
    'does not crash on a stale scheme index ($type)',
    (action) => {
      const s = withLadder({ manager: true, pick: 99 })
      expect(() => reducer(s, action)).not.toThrow()
      expect(reducer(s, action).message?.text).toBeTruthy()
    },
  )
})

describe('ladder editor: an empty ladder', () => {
  it('keeps a usable cursor and refuses to enter the edit', () => {
    const empty = withLadder({ schemes: [{ name: 'plc_empty', title: '', rungs: [], modified: '' }], active: 0 })
    expect(() => ladderCursor(empty)).not.toThrow()
    expect(ladderCursor(empty).row).toBe(0)
    const s = reducer(empty, { type: 'LADDER_EDIT', on: true })
    expect(s.ladder.draft).toBeNull()
    expect(s.message?.text).toBeTruthy()
  })
})
