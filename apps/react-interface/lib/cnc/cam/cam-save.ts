// The write of the quick program (PLAN-QUICKPROG, 7.4, C4): the `CAM_SAVE` branch of `cam.ts`.
//
// The reducer of `cam.ts` edits the document and recomputes the preview; only the save hands the
// result to the files, through `setLines` (`files.ts`) as the one point of write. The module keeps
// the refusal of a busy machine, the name of the file, the confirmation of a replacement and the
// three files that go to the panel: the old lines as `.bak`, the G-code as `.ngc`, the model as
// `.qprog`. The time comes from the action, never from the reducer.
//
// The save runs the checks in one order (PLAN-QUICKPROG, 7.5): `validate` first, then
// `buildProgram` on a valid document, then `checkProgram` on the path. A collision error blocks the
// write and nothing changes; a collision warning is printed into the file as a comment and the write
// goes on, the message naming how many warnings the file carries.
import { putFile } from '../files'
import { isBusy } from '../guards'
import { checkProgram, type CollisionResult } from './collision'
import { emitProgram } from './emit'
import type { QuickProgram } from './ops'
import { buildProgram } from './path'
import { statsOf } from './cam-stats'
import { serializeProgram } from './serialize'
import { validate } from './validate'
import type { Result } from '../subsystem'
import type { MachineState } from '../types'
import type { CamState } from './types'

/** The window in which a second press on «Сохранить» replaces the file without a new question. */
export const SAVE_CONFIRM_MS = 5_000

/**
 * The emitted file of the document, or null when the document cannot be planned. The preview (in
 * `cam.ts`) and the save read the same function, so what the operator saved is what the operator
 * saw. The save reads `cam.program`, not `cam.preview`: a field edit waiting for its timer does not
 * hold back the file, and the preview lines are the file the save just wrote.
 *
 * The warnings of `checkProgram` are `emitted` into the file; a caller that asks for the plan as
 * well gets it and the collision result, so the save does not build the path twice.
 */
export const emitOf = (program: CamState['program'], found?: { current: CollisionResult | null }): string[] | null => {
  const built = buildProgram(program)
  if (!built.ok) return null
  const checked = checkProgram(program, built.path)
  if (found !== undefined) found.current = checked
  return emitProgram(program, built.path, checked.warnings)
}

/** The file name of a save: the program name and the operation count. */
const fileName = (cam: CamState): string => {
  const base = cam.program.name.trim() || 'QUICK'
  return `${base}-${String(cam.program.operations.length)}.ngc`
}

/** The refusal of a save: the machine is not idle. The files do not change. */
const saveBlocked = (s: MachineState): Result | null => {
  if (s.run === 'ESTOP') return { s, msg: 'Сначала сбросьте аварию — запись УП запрещена', level: 'warn' }
  if (isBusy(s)) return { s, msg: 'Остановите программу перед записью УП', level: 'warn' }
  return null
}

/** `<name>.ngc` -> `<stem>`: the base of the `.bak` and the `.qprog` written next to it. */
const stemOf = (name: string): string => name.replace(/\.ngc$/i, '')

/** Two documents are the same write when they plan and emit to the same lines. */
const sameDocument = (a: QuickProgram, b: QuickProgram): boolean => {
  const left = emitOf(a)
  const right = emitOf(b)
  return left !== null && right !== null && left.join('\n') === right.join('\n')
}

/**
 * Whether the pending confirmation still covers this press: the same file, the document the
 * operator confirmed and the press within the window. Without `at` on the action the time is not
 * known and the confirmation does not expire, so a store without stamps still asks once.
 */
const confirmed = (cam: CamState, name: string, at: number | undefined): boolean => {
  const pending = cam.pendingSave
  if (pending === null || pending.name !== name) return false
  // The document changed between the presses: what was confirmed is not what would be written.
  if (!sameDocument(pending.program, cam.program)) return false
  return at === undefined || (at >= pending.at && at - pending.at <= SAVE_CONFIRM_MS)
}

/**
 * Asks the operator to press «Сохранить» once more; nothing is written. The pending entry keeps the
 * document of the first press, so the second press writes exactly what the operator confirmed.
 * A press without `at` keeps the moment already asked, or zero when there is none.
 */
const askReplace = (s: MachineState, cam: CamState, name: string, at: number | undefined): Result => ({
  s: {
    ...s,
    cam: { ...cam, pendingSave: { name, at: at ?? cam.pendingSave?.at ?? 0, program: cam.program } },
  },
  msg: `Файл ${name} уже есть: нажмите «Сохранить» ещё раз, чтобы заменить`,
  level: 'warn',
})

export const saveCam = (s: MachineState, cam: CamState, at: number | undefined): Result => {
  const blocked = saveBlocked(s)
  if (blocked !== null) return blocked
  if (cam.program.operations.length === 0)
    return { s, msg: 'В программе нет операций: добавьте хотя бы одну', level: 'warn' }
  // The order of the checks (PLAN-QUICKPROG, 7.5): the document, then its plan, then the path. The
  // empty list is an error of the check; the message of the validation names the field beside it.
  const checked = validate(cam.program)
  if (!checked.ok) {
    const first = checked.bad[0]
    return { s, msg: `УП не сохранена: ${first.field}: ${first.reason}`, level: 'error' }
  }
  const found: { current: CollisionResult | null } = { current: null }
  // The plan is built once: its lines are the file, its stats are what the header shows from now on.
  const built = buildProgram(cam.program)
  if (!built.ok) return { s, msg: `УП не сохранена: ${built.problems[0]}`, level: 'error' }
  const lines = emitOf(cam.program, found)
  if (lines === null || found.current === null)
    return { s, msg: 'УП не сохранена: программа не строится', level: 'error' }
  if (found.current.errors.length > 0) {
    const first = found.current.errors[0]
    return { s, msg: `УП не сохранена: ${first.field}: ${first.reason}`, level: 'error' }
  }
  const warnings = found.current.warnings.length
  const name = cam.saved ?? fileName(cam)
  const existing = s.files.find((f) => f.name === name)
  if (existing !== undefined && !confirmed(cam, name, at)) return askReplace(s, cam, name, at)

  // The old lines go beside the program before the write replaces them; neither the backup nor the
  // model takes the focus from the program the operator just saved.
  const stem = stemOf(name)
  const backed = existing === undefined ? s : putFile(s, `${stem}.bak`, `${stem}.bak`, existing.lines, at, false)
  const program = putFile(backed, name, name, lines, at)
  const written = putFile(
    program,
    `${stem}.qprog`,
    `${stem}.qprog`,
    serializeProgram(cam.program).split('\n'),
    at,
    false,
  )
  const backup = existing === undefined ? '' : `; прежняя версия в ${stem}.bak`
  const notes = warnings === 0 ? '' : `, предупреждений ${String(warnings)}`
  return {
    // The file is new to the operator: its first start asks for the confirmation (PLAN-QUICKPROG, 8).
    s: {
      ...written,
      cam: { ...cam, saved: name, preview: lines, stats: statsOf(built.path), pending: false, pendingSave: null },
      camStartConfirmed: { name, pendingStart: null },
    },
    msg: `УП записана в файл ${name} — ${String(lines.length)} строк${notes}, модель — в ${stem}.qprog${backup}`,
  }
}
