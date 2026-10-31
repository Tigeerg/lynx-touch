// The insert of the quick program into the open file (PLAN-QUICKPROG, 7.4): the `CAM_INSERT`
// branch of `cam.ts`.
//
// The save writes a whole file with its frame and footer; the insert adds only the modal preamble
// and the operations to the file the operator already has open. The document tags, the generator
// line, the parking footer and the `%` stay out: the open file keeps its own stock, origin and end.
// The block lands before the final `M30` (or before the last `%`), so the existing program still
// ends as it did.
//
// The check order is the save's: validate the document, build the path, refuse on a collision
// error. A warning is emitted as a comment inside the inserted block, as the save does.
import { setLines } from '../files'
import { isBusy } from '../guards'
import type { Result } from '../subsystem'
import type { MachineState } from '../types'
import { checkProgram } from './collision'
import { emitInsert } from './emit'
import { buildProgram } from './path'
import type { CamState } from './types'
import { validate } from './validate'

const LAST_SEQUENCE = /^\s*N(\d+)\b/i
const END_OF_PROGRAM = /^\s*(?:N\d+\s+)?M30\b/i

/** The file the insert lands in, or null when the panel has no active file. */
export function insertTarget(s: MachineState): { name: string; lines: string[] } | null {
  const file = s.files[s.activeFile]
  return file === undefined ? null : { name: file.name, lines: file.lines }
}

/**
 * The open file with the quick-program block before its final `M30`. The trailing `%` and the
 * `M30` stay after the block; a file without them gets the block at the end.
 */
export function mergeInsert(lines: string[], snippet: string[], name: string): string[] {
  const block = [`(QUICKPROG INSERT: ${name})`, ...snippet]
  for (let i = lines.length - 1; i >= 0; i--) {
    if (END_OF_PROGRAM.test(lines[i])) return [...lines.slice(0, i), ...block, ...lines.slice(i)]
  }
  if (lines.length > 0 && lines[lines.length - 1].trim() === '%') {
    return [...lines.slice(0, -1), ...block, lines[lines.length - 1]]
  }
  return [...lines, ...block]
}

/** The next free sequence number of the open file: the last `N` plus one step, or the start. */
const nextSequence = (lines: string[]): number =>
  lines.reduce((max, line) => {
    const found = line.match(LAST_SEQUENCE)
    return found ? Math.max(max, Number(found[1])) : max
  }, 0) + 10

/** The refusal of an insert: a busy machine, a stop, or an empty document. No file changes. */
const blocked = (s: MachineState, cam: CamState): Result | null => {
  if (insertTarget(s) === null) return { s, msg: 'Нет открытой программы: сначала откройте файл', level: 'warn' }
  if (s.run === 'ESTOP') return { s, msg: 'Аварийный стоп: вставка в УП запрещена', level: 'warn' }
  if (isBusy(s)) return { s, msg: 'Выполняется программа: вставка в УП запрещена', level: 'warn' }
  const valid = validate(cam.program)
  if (!valid.ok) return { s, msg: valid.bad[0]?.reason ?? 'Документ не прошёл проверку', level: 'error' }
  return null
}

/** `CAM_INSERT`: the quick program into the active file, with the same checks as the save. */
export function insertCam(s: MachineState, cam: CamState, at?: number): Result {
  const refusal = blocked(s, cam)
  if (refusal !== null) return refusal
  const target = insertTarget(s)!
  const built = buildProgram(cam.program)
  if (!built.ok) return { s, msg: built.problems[0] ?? 'Программа не строится', level: 'error' }
  const checked = checkProgram(cam.program, built.path)
  if (checked.errors.length > 0) return { s, msg: `Вставка остановлена: ${checked.errors[0].reason}`, level: 'error' }
  const snippet = emitInsert(cam.program, built.path, checked.warnings, nextSequence(target.lines))
  const lines = mergeInsert(target.lines, snippet, cam.program.name.trim() || 'QUICK')
  return { s: setLines(s, lines, at), msg: `Вставлено ${String(snippet.length)} строк в ${target.name}` }
}
