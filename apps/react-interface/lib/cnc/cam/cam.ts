// The quick-program subsystem (PLAN-QUICKPROG, 7.4, C4): the `CAM_*` actions in the registry.
//
// `CamState` is a branch of `MachineState` (`cam: CamState | null`), and the actions reach it the
// same way the other subsystems do: `cam.ts` declares the union and the predicate, `machine.ts`
// registers the entry, `subsystem.ts` holds the shape. A subsystem does not import `machine.ts`.
//
// The document is not the machine: the overlay edits a `QuickProgram`, and only `CAM_SAVE` hands
// the result to the files. Writing goes through `setLines` (`files.ts`) as the one point of write:
// the operator gets the G-code in the panel's file list and opens it in the editor.
//
// The form keeps strings (`form.ts`), the preview is recomputed after every edit (the first version
// is synchronous; the budget is measured, not guessed), and `CAM_ACCEPT` runs the same `validate`
// as the planner. A refusal is a message to the operator and the document does not change.
import { putFile } from '../files'
import { isBusy } from '../guards'
import { newProgram } from './defaults'
import { emitProgram } from './emit'
import type { Operation } from './ops'
import { buildProgram } from './path'
import type { Result } from '../subsystem'
import type { MachineState } from '../types'
import type { CamForm } from './form'
import { formOperation, formProblems, newForm, valuesOf } from './form'

/** The overlay state: the document, the form being edited, the last preview and the file written. */
export interface CamState {
  program: ReturnType<typeof newProgram>
  /** The form of a new or edited operation; null when the operator is looking at the list. */
  form: CamForm | null
  /** The index of the operation the form edits; null while the form builds a new one. */
  editing: number | null
  /** The last problem of the form, bound to the field the operator sees. */
  error: { field: string; reason: string } | null
  /** The lines the preview draws: the emitted file of the current document. */
  preview: string[] | null
  /** The name of the file the last `CAM_SAVE` wrote; the next save replaces it. */
  saved: string | null
}

export type CamAction =
  | { type: 'CAM_OPEN' }
  | { type: 'CAM_CLOSE' }
  | { type: 'CAM_OP'; kind: Operation['kind'] }
  | { type: 'CAM_PARAM'; field: string; value: string }
  | { type: 'CAM_CANCEL' }
  | { type: 'CAM_ACCEPT' }
  | { type: 'CAM_EDIT'; index: number }
  | { type: 'CAM_REMOVE'; index: number }
  | { type: 'CAM_TOGGLE'; index: number }
  | { type: 'CAM_MOVE'; index: number; up: boolean }
  | { type: 'CAM_SAVE'; at?: number }

const CAM_TYPES = new Set<string>([
  'CAM_OPEN',
  'CAM_CLOSE',
  'CAM_OP',
  'CAM_PARAM',
  'CAM_CANCEL',
  'CAM_ACCEPT',
  'CAM_EDIT',
  'CAM_REMOVE',
  'CAM_TOGGLE',
  'CAM_MOVE',
  'CAM_SAVE',
])

export const isCamAction = (a: { type: string }): a is CamAction => CAM_TYPES.has(a.type)

/** The overlay of a state; a CAM_* action without an open overlay is a no-op with a hint. */
const closed = (s: MachineState): Result => ({ s, msg: 'Быстрая УП не открыта', level: 'warn' })

/**
 * The emitted file of the document, or null when the document cannot be planned. The preview and
 * the save read the same function, so what the operator saved is what the operator saw.
 */
const emitOf = (program: CamState['program']): string[] | null => {
  const built = buildProgram(program)
  return built.ok ? emitProgram(program, built.path) : null
}

/** The state with the typed field applied and the problem and the preview recomputed. */
const edited = (cam: CamState, field: string, value: string): CamState => {
  if (cam.form === null) return cam
  // The axial step follows the depth of a new form until the operator sets it: a default step deeper
  // than the depth just typed is not a mistake of the operator but a stale default.
  const nextValues = { ...cam.form.values, [field]: value }
  const values = field === 'depthMm' && cam.editing === null ? { ...nextValues, stepDownMm: value } : nextValues
  const form: CamForm = { ...cam.form, values }
  const op = formOperation(form, cam.editing === null ? null : cam.program.operations[cam.editing])
  const problem = formProblems(cam.program, op)[0] ?? null
  const preview = emitOf({ ...cam.program, operations: [...cam.program.operations, op] })
  return { ...cam, form: { ...form, error: problem }, error: problem, preview }
}

/** The operations list with one index replaced; the index outside the list leaves the list. */
const withOperation = (cam: CamState, index: number, next: Operation | null): CamState => {
  if (index < 0 || index >= cam.program.operations.length) return cam
  const operations =
    next === null
      ? cam.program.operations.filter((_, i) => i !== index)
      : cam.program.operations.map((op, i) => (i === index ? next : op))
  const program = { ...cam.program, operations }
  return { ...cam, program, preview: emitOf(program), error: null }
}

/** The file name of a save: the program name, the operation count and the moment of the write. */
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

const save = (s: MachineState, cam: CamState, at: number | undefined): Result => {
  const blocked = saveBlocked(s)
  if (blocked !== null) return blocked
  if (cam.program.operations.length === 0)
    return { s, msg: 'В программе нет операций: добавьте хотя бы одну', level: 'warn' }
  const lines = emitOf(cam.program)
  if (lines === null) {
    const built = buildProgram(cam.program)
    const reason = built.ok ? '' : built.problems[0]
    return { s, msg: `УП не сохранена: ${reason}`, level: 'error' }
  }
  const name = cam.saved ?? fileName(cam)
  const written = putFile(s, name, name, lines, at)
  return {
    s: { ...written, cam: { ...cam, saved: name, preview: lines } },
    msg: `УП записана в файл ${name} — ${String(lines.length)} строк`,
  }
}

export function camReducer(s: MachineState, a: CamAction): Result {
  switch (a.type) {
    case 'CAM_OPEN': {
      if (isBusy(s)) return { s, msg: 'Остановите программу перед быстрой УП', level: 'warn' }
      if (s.run === 'ESTOP') return { s, msg: 'Сначала сбросьте аварию', level: 'warn' }
      const cam: CamState = {
        program: newProgram(),
        form: null,
        editing: null,
        error: null,
        preview: null,
        saved: null,
      }
      return { s: { ...s, cam } }
    }

    case 'CAM_CLOSE':
      return { s: { ...s, cam: null } }

    case 'CAM_OP': {
      if (s.cam === null) return closed(s)
      const form = newForm(a.kind, s.cam.program)
      const op = formOperation(form, null)
      const problem = formProblems(s.cam.program, op)[0] ?? null
      const preview = emitOf({ ...s.cam.program, operations: [...s.cam.program.operations, op] })
      return {
        s: { ...s, cam: { ...s.cam, form: { ...form, error: problem }, editing: null, error: problem, preview } },
      }
    }

    case 'CAM_PARAM':
      return s.cam === null ? closed(s) : { s: { ...s, cam: edited(s.cam, a.field, a.value) } }

    case 'CAM_CANCEL':
      return s.cam === null ? closed(s) : { s: { ...s, cam: { ...s.cam, form: null, editing: null, error: null } } }

    case 'CAM_ACCEPT': {
      if (s.cam === null) return closed(s)
      const cam = s.cam
      if (cam.form === null) return { s, msg: 'Сначала выберите операцию', level: 'warn' }
      const editing = cam.editing === null ? null : (cam.program.operations[cam.editing] ?? null)
      const op = formOperation(cam.form, editing)
      const problem = formProblems(cam.program, op)[0]
      if (problem !== undefined) {
        return {
          s: { ...s, cam: { ...cam, form: { ...cam.form, error: problem }, error: problem } },
          msg: problem.reason,
          level: 'warn',
        }
      }
      const operations =
        cam.editing === null
          ? [...cam.program.operations, op]
          : cam.program.operations.map((old, i) => (i === cam.editing ? op : old))
      const program = { ...cam.program, operations }
      return {
        s: { ...s, cam: { ...cam, program, form: null, editing: null, error: null, preview: emitOf(program) } },
        msg: cam.editing === null ? 'Операция добавлена' : 'Операция изменена',
      }
    }

    case 'CAM_EDIT': {
      if (s.cam === null) return closed(s)
      const op = s.cam.program.operations[a.index]
      if (op === undefined) return { s, msg: 'Операции с таким номером нет', level: 'warn' }
      const form: CamForm = { op: op.kind, values: valuesOf(op), error: null }
      return { s: { ...s, cam: { ...s.cam, form, editing: a.index, error: null } } }
    }

    case 'CAM_REMOVE':
      return s.cam === null ? closed(s) : { s: { ...s, cam: withOperation(s.cam, a.index, null) } }

    case 'CAM_TOGGLE': {
      if (s.cam === null) return closed(s)
      const op = s.cam.program.operations[a.index]
      if (op === undefined) return { s, msg: 'Операции с таким номером нет', level: 'warn' }
      return { s: { ...s, cam: withOperation(s.cam, a.index, { ...op, enabled: !op.enabled }) } }
    }

    case 'CAM_MOVE': {
      if (s.cam === null) return closed(s)
      const list = s.cam.program.operations
      const to = a.up ? a.index - 1 : a.index + 1
      if (a.index < 0 || a.index >= list.length || to < 0 || to >= list.length) return { s }
      const operations = [...list]
      const moved = operations[a.index]
      operations[a.index] = operations[to]
      operations[to] = moved
      const program = { ...s.cam.program, operations }
      return { s: { ...s, cam: { ...s.cam, program, preview: emitOf(program) } } }
    }

    case 'CAM_SAVE':
      return s.cam === null ? closed(s) : save(s, s.cam, a.at)
  }
}
