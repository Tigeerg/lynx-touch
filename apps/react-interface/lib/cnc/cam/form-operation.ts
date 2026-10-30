// The operation of the form and the problems it would have (PLAN-QUICKPROG, 6 and 7.4).
//
//   values -> operation   `formOperation`, the new or the edited operation with the typed fields
//   operation -> problems `formProblems`, the rules of `validate` run over the operation alone
//
// The fields of the form themselves live in `form-fields.ts`; `form.ts` is the facade of both.
import { newOperation } from './defaults'
import type { CamForm } from './form-fields'
import { applyFields, kept, num, str } from './form-fields'
import { profileOf } from './contour-points'
import { probeProfileOf } from './probe-points'
import { counterboreWarnings } from './validate-counterbore'
import type { Operation, OperationKind, QuickProgram } from './ops'
import type { Problem } from './validate'
import { numberFields, validate } from './validate'

/** The operation of a fresh form of the kind, with the typed fields applied on top. */
function build(kind: OperationKind, values: Record<string, string>): Operation {
  const op = newOperation(kind, {
    id: str(values, 'id', ''),
    topZ: kept(values, 'topZ', 0),
  })
  return applyFields(op, values)
}

/** The operation of the form: a new one of its kind, or the edited one, with the typed fields. */
export function formOperation(form: CamForm, editing: Operation | null): Operation {
  const op = editing !== null && editing.kind === form.op ? editing : build(form.op, form.values)
  return applyFields(op, form.values)
}

/**
 * The problems of the operation on its own: it is put into a document of just itself and run
 * through `validate`, so the form and `CAM_ACCEPT` read the same rules in the same order. The
 * operation is checked as enabled even when the operator switched it off: a disabled operation is
 * still expected to be sound when switched on. An empty list means the form can be accepted.
 *
 * A typed field that is not a number yet (`'abc'`) is refused at its input: the operation holds the
 * model value (`build`), so the check reads the text the operator sees and, from `numberFields`,
 * the input it was typed into (`stepOverPct`, `rect.w`), rather than the rule the reason names.
 */
export function formProblems(program: QuickProgram, values: Record<string, string>, op: Operation): Problem[] {
  // A new form copies the depth into the axial step until the operator sets the step (`depth` and
  // `step` are one value), so a typed depth answers for the step the operator did not touch. A
  // chamfer's depth is its own leg: the same copy keeps the frame the operator reads consistent.
  const fixed = chamferDepth(stepOfDepth(values, op), values)
  const typed = numberFields(op).filter((item) => !Number.isFinite(num(values, item.form, item.value)))
  if (typed.length > 0) {
    return typed.map((item) => ({ field: item.form, reason: `Поле «${item.label}» не число: введите число.` }))
  }
  const badPoint = pointProblem(op, values)
  if (badPoint !== null) return [badPoint]
  // A counterbore finer than its own diameter blocks nothing: its reason is a warning of the form
  // and comes with the blocking problems, so one list answers both questions without a second run.
  const checked = validate({ ...program, operations: [{ ...fixed, enabled: true }] })
  const warnings = fixed.kind === 'counterbore' ? counterboreWarnings(fixed) : []
  return checked.ok ? warnings : [...checked.bad, ...warnings]
}

/**
 * The first pair of the points field that is not two numbers, by its number in the list. The field
 * holds a list, not one number, so `numberFields` skips it and the text is read here.
 */
function pointProblem(op: Operation, values: Record<string, string>): Problem | null {
  if (op.kind !== 'contour' && op.kind !== 'probe') return null
  const text = values.points
  if (text === undefined || text.trim() === '') return null
  const points = op.kind === 'contour' ? profileOf(text, op.topZ) : probeProfileOf(text)
  const at = points.findIndex((point) => point === null)
  if (at < 0) return null
  const shape = op.kind === 'contour' ? 'двумя числами X и Y' : 'тремя числами X, Y и Z'
  return { field: 'points', reason: `Точка ${at + 1} задана не ${shape}.` }
}

/**
 * The operation with the axial step of its depth when the depth is a number and the step is not:
 * the reducer writes the depth into the step on every keystroke of `depthMm` (`cam.ts`, `edited`),
 * so the step follows the depth while the operator types. Every other field stays as built.
 */
const stepOfDepth = (values: Record<string, string>, op: Operation): Operation => {
  // The step follows the depth only while the operator has not typed a step of its own: an absent
  // step, one that is not a number, or the same text as the depth (the copy of the reducer) is the
  // depth's, and the depth answers for both. A step typed on its own stays its own.
  const follows = values.stepDownMm === undefined || !Number.isFinite(num(values, 'stepDownMm', op.stepDownMm))
  if (!follows) return op
  const depth = num(values, 'depthMm', op.depthMm)
  if (!Number.isFinite(depth)) return op
  return { ...op, stepDownMm: depth }
}

/**
 * The depth of a chamfer follows its leg: the bottom of the cut is `topZ - chamferMm`, so a typed
 * chamfer writes the depth and the frame the operator reads stays the one the planner cuts.
 */
const chamferDepth = (op: Operation, values: Record<string, string>): Operation => {
  if (op.kind !== 'chamfer' || !Number.isFinite(num(values, 'chamferMm', op.chamferMm))) return op
  return { ...op, depthMm: num(values, 'chamferMm', op.chamferMm) }
}
