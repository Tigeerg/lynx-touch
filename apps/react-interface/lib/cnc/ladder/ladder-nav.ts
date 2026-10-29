// The ladder navigation helpers: the active scheme and its rungs, the cursor over a rung and its
// parallel branches, and the contacts of a path with their write-back. Kept out of ladder-reducer.ts
// so the element cases (ladder-forms.ts) and the scheme manager (ladder-schemes.ts) read them
// without importing the reducer's action union; ladder-reducer.ts re-exports them, so the public
// surface (via the './ladder' facade) is the same. The data comes from ladder-data.ts; the rung
// copy (the edit draft) lives in the leaf ladder-rung.ts, so this module pulls no schemes or form.
import { clampIndex } from '../clamp'
import { rungBranches } from './ladder-data'
import type { LadderContact, LadderRung, MachineState } from '../types'

/** The active scheme; a stale index falls back to the first one rather than reading undefined. */
export const activeScheme = (s: MachineState) => s.ladder.schemes[s.ladder.active] ?? s.ladder.schemes[0]
export const savedRungs = (s: MachineState) => activeScheme(s)?.rungs ?? []
export const ladderRungs = (s: MachineState) => s.ladder.draft ?? savedRungs(s)

/** What the editor and the views read when a scheme has no rungs at all (invalid state). */
const EMPTY_RUNG: LadderRung = { title: '', contacts: [], coil: { addr: 'M1.0', label: '' } }

export function ladderCursor(s: MachineState) {
  const rungs = ladderRungs(s)
  const row = clampIndex(s.cursor.service, 0, rungs.length)
  const rung = rungs[row] ?? EMPTY_RUNG
  const branches = rungBranches(rung)
  const path = clampIndex(s.ladder.path, 0, branches.length)
  const contacts = branches[path] ?? []
  const maxCol = path === 0 ? contacts.length : Math.max(0, contacts.length - 1)
  const col = clampIndex(s.ladder.col, 0, maxCol + 1)
  return { rungs, row, rung, branches, path, contacts, col, onCoil: path === 0 && col === contacts.length }
}

export const pathContacts = (rung: LadderRung, path: number) => rungBranches(rung)[path] ?? rung.contacts

export const withPathContacts = (rung: LadderRung, path: number, contacts: LadderContact[]): LadderRung =>
  path === 0 ? { ...rung, contacts } : { ...rung, or: (rung.or ?? []).map((b, i) => (i === path - 1 ? contacts : b)) }
