// Файлы управляющих программ и правка строк УП: общий источник для машины
// (machine.ts) и подсистемы экрана «Программа» (prog.ts). Вынесено, чтобы редуктор
// сводил подсистемы и не рос (метка tools/check.ps1); компоненты берут
// currentFile/programLines из machine.ts (реэкспорт).
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
