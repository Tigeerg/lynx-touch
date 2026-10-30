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

/**
 * Writes the lines into the file `name`: an existing file of that name is replaced, a new one is
 * appended. The second point of write next to `setLines`, for a subsystem that produces a whole
 * file (the quick program) instead of editing the active one.
 *
 * `active` says whether the written file becomes the one the editor and the screen show. The
 * quick program writes the `.bak` and the `.qprog` beside its NC file, so those go in with
 * `active: false` and the operator still sees the program that was just saved.
 */
export const putFile = (
  s: MachineState,
  name: string,
  title: string,
  lines: string[],
  at?: number,
  active = true,
): MachineState => {
  const modified = at === undefined ? '' : formatStamp(at)
  const i = s.files.findIndex((f) => f.name === name)
  if (i >= 0)
    return {
      ...s,
      files: s.files.map((f, k) => (k === i ? { ...f, title, lines, modified } : f)),
      activeFile: active ? i : s.activeFile,
    }
  return {
    ...s,
    files: [...s.files, { name, title, lines, modified }],
    activeFile: active ? s.files.length : s.activeFile,
  }
}
