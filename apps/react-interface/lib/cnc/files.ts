// NC program files and program-line editing: a shared source for the machine (machine.ts)
// and the Program screen subsystem (prog.ts). Extracted so the reducer composes the
// subsystems and does not grow (tools/check.ps1 mark); components take currentFile/
// programLines from machine.ts (re-export).
import type { MachineState } from './types'

export const currentFile = (s: MachineState) => s.files[s.activeFile]
export const programLines = (s: MachineState) => currentFile(s).lines

export const stamp = () =>
  new Date()
    .toLocaleString('ru-RU', { day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit' })
    .replace(',', '')

export const setLines = (s: MachineState, lines: string[]): MachineState => ({
  ...s,
  files: s.files.map((f, i) => (i === s.activeFile ? { ...f, lines, modified: stamp() } : f)),
})
