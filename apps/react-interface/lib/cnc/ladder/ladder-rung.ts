// The rung builders: the free coil address picker, the new rung and the deep copy of rungs for the
// edit draft. Kept as a leaf so the scheme manager and the reducer build rungs without importing
// each other (the naive split of a107fbe made a reducer -> forms -> schemes -> reducer cycle). The
// only imports allowed here are the data module and the shared types.
import type { LadderContact, LadderRung } from '../types'
import { sig } from './ladder-data'

export type FreeAddrKind = 'flag' | 'timer' | 'counter'

/**
 * The first free coil address of a kind for a new rung: it must not already be on the scheme (a
 * duplicate coil is refused on save). One function, not three near-copies.
 */
export function freeAddr(rungs: LadderRung[], kind: FreeAddrKind): string {
  const used = new Set(rungs.map((r) => r.coil.addr))
  if (kind === 'flag') {
    for (let byte = 1; byte < 100; byte++) {
      for (let bit = 0; bit < 8; bit++) {
        const addr = `M${byte}.${bit}`
        if (!used.has(addr)) return addr
      }
    }
    return 'M1.0'
  }
  const prefix = kind === 'timer' ? 'T' : 'C'
  for (let n = 0; n < 100; n++) if (!used.has(`${prefix}${n}`)) return `${prefix}${n}`
  return `${prefix}99`
}

export const newRung = (rungs: LadderRung[], n: number): LadderRung => ({
  title: `Ступень ${n}`,
  contacts: [sig('I0.0')],
  coil: { addr: freeAddr(rungs, 'flag'), label: 'Новый флаг' },
})

const cloneContact = (c: LadderContact): LadderContact => ({ ...c, ...(c.cmp ? { cmp: { ...c.cmp } } : {}) })
/** A deep copy of the rungs for the edit draft: a draft edit never reaches the saved scheme. */
export const cloneRungs = (rungs: LadderRung[]): LadderRung[] =>
  rungs.map((r) => ({
    ...r,
    contacts: r.contacts.map(cloneContact),
    coil: { ...r.coil },
    ...(r.or ? { or: r.or.map((b) => b.map(cloneContact)) } : {}),
  }))
