// The form of the overlay as the reducer changes it (PLAN-QUICKPROG, 7.4 and 7.5): one typed value
// with its problem and the pending flag, the frame of a new form, the accept that turns the form
// into an operation of the list, and the actions over that list.
//
// The form keeps strings; the operation of the form is the one the check and the accept read. The
// accept binds a refusal to the field the operator sees and leaves the document as it was. Every
// recompute of the preview takes the lines AND the stats of the same plan (`shownPreview`), so the
// header never describes another program than the one on the screen.
import type { Result } from '../subsystem'
import type { MachineState } from '../types'
import { shownPreview } from './cam-preview'
import type { CamForm } from './form'
import { formOperation, formProblems, newForm, valuesOf } from './form'
import type { Operation } from './ops'
import type { Problem } from './validate'
import type { CamState } from './types'

/** The form with one typed value, its problem and the preview left for the debounce. */
export type Edit = (cam: CamState, field: string, value: string) => CamState

/**
 * The state with the typed field applied and the problem recomputed. The preview waits: the operator
 * still types, and at least one more keystroke may come within `PREVIEW_DEBOUNCE_MS`.
 */
export const edited: Edit = (cam, field, value) => {
  if (cam.form === null) return cam
  // The axial step follows the depth of a new form until the operator sets it: a default step deeper
  // than the depth just typed is not a mistake of the operator but a stale default.
  const next = { ...cam.form.values, [field]: value }
  const values = field === 'depthMm' && cam.editing === null ? { ...next, stepDownMm: value } : next
  const op = formOperation({ ...cam.form, values }, cam.editing === null ? null : cam.program.operations[cam.editing])
  const problem = formProblems(cam.program, values, op)[0] ?? null
  return { ...cam, form: { ...cam.form, values, error: problem }, error: problem, pending: true }
}

/** The overlay with a fresh form of `kind` over the document, its preview and its numbers at once. */
export function shownForm(cam: CamState, kind: Operation['kind']): CamState {
  const form = newForm(kind, cam.program)
  const op = formOperation(form, null)
  const problem = formProblems(cam.program, form.values, op)[0] ?? null
  const program = { ...cam.program, operations: [...cam.program.operations, op] }
  return {
    ...cam,
    form: { ...form, error: problem },
    editing: null,
    error: problem,
    ...shownPreview(program, cam),
    pending: false,
  }
}

/** The form of the overlay with the problem bound to the field the operator sees. */
const asForm = (cam: CamState, problem: Problem): CamState =>
  cam.form === null ? cam : { ...cam, form: { ...cam.form, error: problem }, error: problem }

/** The accept of the form: the operation joins the list, or replaces the one under edit, or a refusal. */
export function acceptRun(s: MachineState, cam: CamState): Result {
  if (cam.form === null) return { s, msg: 'Сначала выберите операцию', level: 'warn' }
  const editing = cam.editing === null ? null : (cam.program.operations[cam.editing] ?? null)
  const op = formOperation(cam.form, editing)
  const problem = formProblems(cam.program, cam.form.values, op)[0] ?? null
  if (problem !== null) return { s: { ...s, cam: asForm(cam, problem) }, msg: problem.reason, level: 'warn' }
  const message = cam.editing === null ? 'Операция добавлена' : 'Операция изменена'
  return { s: { ...s, cam: shifted(cam, op) }, msg: message }
}

/**
 * The overlay with the operation in the list: appended, or in place of the one under edit. Not
 * typing: the preview is recomputed here, so a field edit that never met its timer leaves nothing
 * stale behind.
 */
const shifted = (cam: CamState, op: Operation): CamState => {
  const operations =
    cam.editing === null
      ? [...cam.program.operations, op]
      : cam.program.operations.map((old, i) => (i === cam.editing ? op : old))
  const program = { ...cam.program, operations }
  return { ...cam, program, form: null, editing: null, error: null, ...shownPreview(program, cam), pending: false }
}

/**
 * The actions over the list of operations (PLAN-QUICKPROG, 7.4 and 7.6): the form of one operation
 * opened for edit, its on/off, its removal, its move by one place, and the accept of the form.
 */
export function listRun(
  s: MachineState,
  cam: CamState,
  a:
    | { type: 'CAM_ACCEPT' }
    | { type: 'CAM_EDIT' | 'CAM_TOGGLE' | 'CAM_REMOVE' | 'CAM_MOVE'; index: number; up?: boolean },
): Result {
  if (a.type === 'CAM_ACCEPT') return acceptRun(s, cam)
  const op = cam.program.operations[a.index]
  if (a.type !== 'CAM_MOVE' && op === undefined) return { s, msg: 'Операции с таким номером нет', level: 'warn' }
  if (a.type === 'CAM_EDIT' && op !== undefined) {
    const form: CamForm = { op: op.kind, values: valuesOf(op), error: null }
    return { s: { ...s, cam: { ...cam, form, editing: a.index, error: null } } }
  }
  if (a.type === 'CAM_TOGGLE' && op !== undefined)
    return { s: { ...s, cam: setOperation(cam, a.index, { ...op, enabled: !op.enabled }) } }
  if (a.type === 'CAM_REMOVE') return { s: { ...s, cam: setOperation(cam, a.index, null) } }
  const list = cam.program.operations
  const to = a.up === true ? a.index - 1 : a.index + 1
  if (a.index < 0 || a.index >= list.length || to < 0 || to >= list.length) return { s }
  const operations = [...list]
  const moved = operations[a.index]
  operations[a.index] = operations[to]
  operations[to] = moved
  const program = { ...cam.program, operations }
  return { s: { ...s, cam: { ...cam, program, ...shownPreview(program, cam), pending: false } } }
}

/** The document with one operation replaced, removed (`null`) or left as it was for a bad index. */
const setOperation = (cam: CamState, index: number, next: Operation | null): CamState => {
  if (index < 0 || index >= cam.program.operations.length) return cam
  const operations =
    next === null
      ? cam.program.operations.filter((_, i) => i !== index)
      : cam.program.operations.map((op, i) => (i === index ? next : op))
  const program = { ...cam.program, operations }
  return { ...cam, program, ...shownPreview(program, cam), error: null, pending: false }
}
