// The PLC types: a contact, a coil with its kind and preset, a rung with its parallel (OR)
// branches, the scheme, the editor form, the scan memory and the subsystem state. Kept in their
// own module so the ladder data, parse and reducer do not have to reach into types.ts; types.ts
// re-exports them, so the public surface stays the same.
export type CompareOp = '>' | '<' | '>=' | '<=' | '=' | '<>'

export interface LadderCompare {
  op: CompareOp
  a: string
  b: string
}

export interface LadderContact {
  addr: string
  label: string
  nc?: boolean
  /** When set, the element is a comparison block, not a contact. */
  cmp?: LadderCompare
}

export type CoilKind = 'coil' | 'set' | 'reset' | 'ton' | 'tof' | 'ctu' | 'ctd'

export interface LadderCoil extends LadderContact {
  kind?: CoilKind
  /** Timer preset, ms (ton/tof). Missing means DEFAULT_PRESET_MS. */
  presetMs?: number
  /** Counter preset, pulses (ctu/ctd). Missing means DEFAULT_PRESET_COUNT. */
  presetCount?: number
}

export interface LadderRung {
  title: string
  contacts: LadderContact[]
  coil: LadderCoil
  /** Parallel branches (OR): each branch is a sequence of contacts (AND). */
  or?: LadderContact[][]
}

export interface LadderMem {
  timers: Record<string, { acc: number; q: boolean }>
  counters: Record<string, { acc: number; q: boolean; prev: boolean }>
  latched: Record<string, boolean>
}

export interface LadderScheme {
  name: string
  title: string
  rungs: LadderRung[]
  modified: string
}

export type LadderForm =
  | { kind: 'element'; values: [string, string, string, string]; compare?: boolean }
  | { kind: 'scheme'; target: number; values: [string, string] }

export interface LadderState {
  schemes: LadderScheme[]
  active: number
  draft: LadderRung[] | null
  dirty: boolean
  col: number
  /** Branch index: 0 is the main line, 1.. are the parallel (OR) branches. */
  path: number
  manager: boolean
  pick: number
  form: LadderForm | null
  mem: LadderMem
  /** The signal map of the last PLC scan (TICK), so the monitor reads it instead of rescanning. */
  signals: Record<string, boolean>
}
