// NC program files and program-line editing: a shared source for the machine (machine.ts)
// and the Program screen subsystem (prog.ts). Extracted so the reducer composes the
// subsystems and does not grow (tools/check.ps1 mark); components take currentFile/
// programLines from machine.ts (re-export).
import { formatStamp } from './time'
import type { MachineState } from './types'

export const currentFile = (s: MachineState) => s.files[s.activeFile]
export const programLines = (s: MachineState) => currentFile(s).lines

/**
 * Replaces the active file's lines. `at` is the modification mark from the action (epoch ms): a
 * `Date` here made the reducer impure and the tests time-dependent; without it the mark stays.
 */
export const setLines = (s: MachineState, lines: string[], at?: number): MachineState => ({
  ...s,
  files: s.files.map((f, i) =>
    i === s.activeFile ? { ...f, lines, modified: at === undefined ? f.modified : formatStamp(at) } : f,
  ),
})
