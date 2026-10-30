// The quick-program subsystem (PLAN-QUICKPROG, 7.4, C4): the `CAM_*` actions in the registry.
//
// `CamState` is a branch of `MachineState` (`cam: CamState | null`), and the actions reach it the
// same way the other subsystems do: `cam.ts` declares the union and the predicate, `machine.ts`
// registers the entry, `subsystem.ts` holds the shape. A subsystem does not import `machine.ts`.
//
// The document is not the machine: the overlay edits a `QuickProgram`, and only `CAM_SAVE` hands
// the result to the files. The write itself lives in `cam-save.ts` (`saveCam`), next to the refusal
// of a busy machine and the confirmation of a replacement.
//
// The form keeps strings (`form.ts`), and `CAM_ACCEPT` runs the same `validate` as the planner. A
// refusal is a message to the operator and the document does not change.
//
// A field edit and the preview are two different clocks (PLAN-QUICKPROG, 7.5): `CAM_PARAM` applies
// the typed text and the problem at once, marks the preview `pending` and leaves it alone, and
// `CAM_PREVIEW` from the UI timer recomputes it. The list and accept actions are not typing, so
// they recompute straight away and clear the flag. A delayed timer never wins over a save: the
// save reads the document, not the preview.
import { isBusy } from '../guards'
import { arrayCopies, rotatedCopies, type CopyResult } from './copy-ops'
import { newProgram } from './defaults'
import type { Operation } from './ops'
import { buildProgram } from './path'
import { emitOf, saveCam } from './cam-save'
import { dxfResult } from './dxf-result'
import type { Result } from '../subsystem'
import type { MachineState } from '../types'
import type { CamForm } from './form'
import { formOperation, formProblems, newForm, valuesOf } from './form'
import type { CamAction, CamState } from './types'

export type { CamAction, CamState }

const CAM_TYPES = new Set<string>([
  'CAM_OPEN',
  'CAM_CLOSE',
  'CAM_OP',
  'CAM_PARAM',
  'CAM_PREVIEW',
  'CAM_CANCEL',
  'CAM_ACCEPT',
  'CAM_EDIT',
  'CAM_REMOVE',
  'CAM_TOGGLE',
  'CAM_MOVE',
  'CAM_SAVE',
  'CAM_ARRAY',
  'CAM_ROTATE',
  'CAM_DXF',
])

export const isCamAction = (a: { type: string }): a is CamAction => CAM_TYPES.has(a.type)

/** The overlay of a state; a CAM_* action without an open overlay is a no-op with a hint. */
const closed = (s: MachineState): Result => ({ s, msg: 'Быстрая УП не открыта', level: 'warn' })

/**
 * The state with the typed field applied and the problem recomputed. The preview waits: the operator
 * still types, and at least one more keystroke may come within `PREVIEW_DEBOUNCE_MS`.
 */
const edited = (cam: CamState, field: string, value: string): CamState => {
  if (cam.form === null) return cam
  // The axial step follows the depth of a new form until the operator sets it: a default step deeper
  // than the depth just typed is not a mistake of the operator but a stale default.
  const nextValues = { ...cam.form.values, [field]: value }
  const values = field === 'depthMm' && cam.editing === null ? { ...nextValues, stepDownMm: value } : nextValues
  const form: CamForm = { ...cam.form, values }
  const op = formOperation(form, cam.editing === null ? null : cam.program.operations[cam.editing])
  const problem = formProblems(cam.program, values, op)[0] ?? null
  return { ...cam, form: { ...form, error: problem }, error: problem, pending: true }
}

/** The preview of the document with the form's operation, as `edited` used to build it. */
const previewOf = (cam: CamState): string[] | null => {
  if (cam.form === null) return cam.preview
  const editing = cam.editing === null ? null : (cam.program.operations[cam.editing] ?? null)
  const op = formOperation(cam.form, editing)
  return emitOf({ ...cam.program, operations: [...cam.program.operations, op] })
}

/** The operations list with one index replaced; the index outside the list leaves the list. */
const withOperation = (cam: CamState, index: number, next: Operation | null): CamState => {
  if (index < 0 || index >= cam.program.operations.length) return cam
  const operations =
    next === null
      ? cam.program.operations.filter((_, i) => i !== index)
      : cam.program.operations.map((op, i) => (i === index ? next : op))
  const program = { ...cam.program, operations }
  return { ...cam, program, preview: emitOf(program), error: null, pending: false }
}

export function camReducer(s: MachineState, a: CamAction): Result {
  switch (a.type) {
    case 'CAM_OPEN': {
      if (isBusy(s)) return { s, msg: 'Остановите программу перед быстрой УП', level: 'warn' }
      if (s.run === 'ESTOP') return { s, msg: 'Сначала сбросьте аварию', level: 'warn' }
      // The machine parameters travel with the document, not in it: the tap check reads the rigid
      // tapping switch from the same state the panel shows, and a document saved on one machine
      // does not carry the switch of another.
      const cam: CamState = {
        program: { ...newProgram(), params: { ...s.params } },
        form: null,
        editing: null,
        error: null,
        preview: null,
        saved: null,
        pendingSave: null,
      }
      return { s: { ...s, cam } }
    }

    case 'CAM_CLOSE':
      return { s: { ...s, cam: null } }

    case 'CAM_OP': {
      if (s.cam === null) return closed(s)
      const form = newForm(a.kind, s.cam.program)
      const op = formOperation(form, null)
      const problem = formProblems(s.cam.program, form.values, op)[0] ?? null
      const preview = emitOf({ ...s.cam.program, operations: [...s.cam.program.operations, op] })
      return {
        s: {
          ...s,
          cam: { ...s.cam, form: { ...form, error: problem }, editing: null, error: problem, preview, pending: false },
        },
      }
    }

    case 'CAM_PARAM':
      return s.cam === null ? closed(s) : { s: { ...s, cam: edited(s.cam, a.field, a.value) } }

    case 'CAM_PREVIEW': {
      if (s.cam === null) return closed(s)
      const cam = s.cam
      // Without an unfinished edit there is nothing to catch up with; a late tick is a no-op.
      if (cam.pending !== true) return { s }
      return {
        s: { ...s, cam: { ...cam, preview: previewOf(cam), pending: false, previewAt: a.at ?? cam.previewAt } },
      }
    }

    case 'CAM_CANCEL':
      return s.cam === null ? closed(s) : { s: { ...s, cam: { ...s.cam, form: null, editing: null, error: null } } }

    case 'CAM_ACCEPT': {
      if (s.cam === null) return closed(s)
      const cam = s.cam
      if (cam.form === null) return { s, msg: 'Сначала выберите операцию', level: 'warn' }
      const editing = cam.editing === null ? null : (cam.program.operations[cam.editing] ?? null)
      const op = formOperation(cam.form, editing)
      const problem = formProblems(cam.program, cam.form.values, op)[0]
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
        // Not typing: the preview of the document is recomputed here, so a field edit that never met
        // its timer leaves nothing stale behind.
        s: {
          ...s,
          cam: { ...cam, program, form: null, editing: null, error: null, preview: emitOf(program), pending: false },
        },
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
      return { s: { ...s, cam: { ...s.cam, program, preview: emitOf(program), pending: false } } }
    }

    case 'CAM_SAVE':
      return s.cam === null ? closed(s) : saveCam(s, s.cam, a.at)

    case 'CAM_ARRAY': {
      if (s.cam === null) return closed(s)
      const op = s.cam.program.operations[a.index]
      if (op === undefined) return { s, msg: 'Операции с таким номером нет', level: 'warn' }
      return copied(s, s.cam, a.index, arrayCopies(op, a))
    }

    case 'CAM_ROTATE': {
      if (s.cam === null) return closed(s)
      const op = s.cam.program.operations[a.index]
      if (op === undefined) return { s, msg: 'Операции с таким номером нет', level: 'warn' }
      return copied(s, s.cam, a.index, rotatedCopies(op, a))
    }

    case 'CAM_DXF':
      return s.cam === null ? closed(s) : dxfResult(s, s.cam, a.text, a.contour)
  }
}

/**
 * The document with the source and its copies in place of the source, or the refusal as a message.
 * The copies are flattened here, on the action, so the list holds them and the `.qprog` stays flat;
 * a refusal changes nothing but the message line.
 */
const copied = (s: MachineState, cam: CamState, at: number, result: CopyResult): Result => {
  if (!result.ok) return { s, msg: result.reason, level: 'warn' }
  const list = cam.program.operations
  const operations = [...list.slice(0, at), ...result.operations, ...list.slice(at + 1)]
  const program = { ...cam.program, operations }
  return {
    s: { ...s, cam: { ...cam, program, preview: emitOf(program), error: null, pending: false } },
    msg: `Копий: ${String(result.operations.length - 1)}`,
  }
}
