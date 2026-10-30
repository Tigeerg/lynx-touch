// The form of one operation and the way it maps to the document (PLAN-QUICKPROG, 6 and 7.4).
//
// The form keeps strings, as the input fields do: `values` is what the operator typed, so a value
// that is not a number yet stays in the field instead of being silently replaced. The mapping to
// the operation is explicit in both directions:
//
//   values -> operation   `formOperation` / `applyFields`, fields grouped by meaning
//   operation -> values   `valuesOf`, so CAM_EDIT opens the form with the numbers of the document
//
// A new form starts from `defaults.ts`; the shape fields are named the same in the document and in
// the form (`rect.x`, `circle.r`, ...). The reducer turns the operation into the document.
import type { DrillBody, Material } from './feeds'
import { cuttingFor } from './feeds'
import { newOperation } from './defaults'
import type { DrillPattern, Operation, OperationKind, QuickProgram } from './ops'
import { stockTop } from './ops'
import type { Problem } from './validate'
import { validate } from './validate'

/** The form of an operation: the kind, the typed values, and the result of the last edit. */
export interface CamForm {
  /** The kind of the operation the form builds. */
  op: OperationKind
  /** The typed values by field name: `depthMm`, `tool.number`, `rect.w`, ... */
  values: Record<string, string>
  /** The problems of the last edit, each bound to the field the operator sees. */
  error: Problem | null
}

/** A number from a typed value; `NaN` when the field is empty or not a number. */
const num = (values: Record<string, string>, key: string, fallback: number): number => {
  const text = values[key]
  if (text === undefined || text.trim() === '') return fallback
  const value = Number(text.replace(',', '.'))
  return Number.isFinite(value) ? value : Number.NaN
}

/** An integer from a typed value; `NaN` when the field is empty or not an integer. */
const int = (values: Record<string, string>, key: string, fallback: number): number => {
  const value = num(values, key, fallback)
  return Number.isFinite(value) ? Math.trunc(value) : value
}

const str = (values: Record<string, string>, key: string, fallback: string): string => values[key] ?? fallback

const on = (values: Record<string, string>, key: string, fallback: boolean): boolean => {
  const text = values[key]
  if (text === undefined) return fallback
  return text === 'on' || text === 'true' || text === '1' || text === 'да'
}

const cuttingOf = (values: Record<string, string>, material: Material, diameterMm: number, body: DrillBody | null) => {
  const table = cuttingFor(material, diameterMm, body)
  return {
    spindleRpm: num(values, 'cutting.spindleRpm', table.spindleRpm),
    feedMmMin: num(values, 'cutting.feedMmMin', table.feedMmMin),
    plungeFeedMmMin: num(values, 'cutting.plungeFeedMmMin', table.plungeFeedMmMin),
  }
}

/** The typed values of a new operation of `kind` over the top `topZ`, as a form would hold them. */
export const formValues = (kind: OperationKind, topZ: number): Record<string, string> =>
  valuesOf(newOperation(kind, { id: '', topZ }))

/** A new form of `kind`: the defaults of the operation over the stock top. */
export const newForm = (kind: OperationKind, program: QuickProgram): CamForm => ({
  op: kind,
  values: formValues(kind, stockTop(program.stock)),
  error: null,
})

const baseValues = (op: Operation): Record<string, string> => ({
  id: op.id,
  enabled: op.enabled ? 'on' : 'off',
  'tool.number': String(op.tool.number),
  'tool.diameterMm': String(op.tool.diameterMm),
  'tool.hNumber': String(op.tool.hNumber),
  material: op.material,
  coolant: op.coolant,
  topZ: String(op.topZ),
  depthMm: String(op.depthMm),
  stepDownMm: String(op.stepDownMm),
  notes: op.notes,
})

/**
 * The typed values of an operation: the common fields and the ones of its own kind. The narrow
 * switch is what makes a forgotten kind a compile error.
 */
export function valuesOf(op: Operation): Record<string, string> {
  const values: Record<string, string> = {
    ...baseValues(op),
    'cutting.spindleRpm': String(op.cutting.spindleRpm),
    'cutting.feedMmMin': String(op.cutting.feedMmMin),
    'cutting.plungeFeedMmMin': String(op.cutting.plungeFeedMmMin),
  }
  switch (op.kind) {
    case 'face':
      return { ...values, z: String(op.z), stepOverPct: String(op.stepOverPct) }
    case 'pocket':
      return {
        ...values,
        'rect.x': String(op.rect.x),
        'rect.y': String(op.rect.y),
        'rect.w': String(op.rect.w),
        'rect.h': String(op.rect.h),
        cornerRadiusMm: String(op.cornerRadiusMm),
        finishAllowanceMm: String(op.finishAllowanceMm),
        stepOverPct: String(op.stepOverPct),
      }
    case 'circle-pocket':
      return {
        ...values,
        'circle.x': String(op.circle.x),
        'circle.y': String(op.circle.y),
        'circle.r': String(op.circle.r),
        finishAllowanceMm: String(op.finishAllowanceMm),
        stepOverPct: String(op.stepOverPct),
      }
    case 'drill':
      return {
        ...values,
        pattern: op.pattern,
        body: op.body,
        peckMm: String(op.peckMm),
        'hole.x': String(op.hole.x),
        'hole.y': String(op.hole.y),
        'hole.r': String(op.hole.r),
        x0: String(op.x0),
        y0: String(op.y0),
        cols: String(op.cols),
        rows: String(op.rows),
        dx: String(op.dx),
        dy: String(op.dy),
        count: String(op.count),
        startDeg: String(op.startDeg),
        arcDeg: String(op.arcDeg),
        fullTurn: op.fullTurn ? 'on' : 'off',
      }
  }
}

/** The operation of a fresh form of the kind, with the typed fields applied on top. */
function build(kind: OperationKind, values: Record<string, string>): Operation {
  const op = newOperation(kind, {
    id: str(values, 'id', ''),
    topZ: num(values, 'topZ', 0),
  })
  return applyFields(op, values)
}

/**
 * The common fields of an operation from the typed values: the tool, the cutting data (the table
 * is the fallback of an empty field, as the console does), the frame and the notes.
 */
function applyCommon<T extends Operation>(op: T, values: Record<string, string>): T {
  const number = int(values, 'tool.number', op.tool.number)
  const diameterMm = num(values, 'tool.diameterMm', op.tool.diameterMm)
  const material = str(values, 'material', op.material) as Material
  return {
    ...op,
    id: str(values, 'id', op.id),
    enabled: on(values, 'enabled', op.enabled),
    tool: { number, diameterMm, hNumber: int(values, 'tool.hNumber', number) },
    cutting: cuttingOf(values, material, diameterMm, op.kind === 'drill' ? op.body : null),
    coolant: str(values, 'coolant', op.coolant) as Operation['coolant'],
    material,
    topZ: num(values, 'topZ', op.topZ),
    depthMm: num(values, 'depthMm', op.depthMm),
    stepDownMm: num(values, 'stepDownMm', op.stepDownMm),
    notes: str(values, 'notes', op.notes),
  }
}

/** Writes the typed fields of the kind onto the operation; the narrow switch covers every kind. */
function applyFields(op: Operation, v: Record<string, string>): Operation {
  switch (op.kind) {
    case 'face':
      return {
        ...applyCommon(op, v),
        z: num(v, 'z', op.z),
        stepOverPct: num(v, 'stepOverPct', op.stepOverPct),
      }
    case 'pocket':
      return {
        ...applyCommon(op, v),
        rect: {
          x: num(v, 'rect.x', op.rect.x),
          y: num(v, 'rect.y', op.rect.y),
          w: num(v, 'rect.w', op.rect.w),
          h: num(v, 'rect.h', op.rect.h),
        },
        cornerRadiusMm: num(v, 'cornerRadiusMm', op.cornerRadiusMm),
        finishAllowanceMm: num(v, 'finishAllowanceMm', op.finishAllowanceMm),
        stepOverPct: num(v, 'stepOverPct', op.stepOverPct),
      }
    case 'circle-pocket':
      return {
        ...applyCommon(op, v),
        circle: {
          x: num(v, 'circle.x', op.circle.x),
          y: num(v, 'circle.y', op.circle.y),
          r: num(v, 'circle.r', op.circle.r),
        },
        finishAllowanceMm: num(v, 'finishAllowanceMm', op.finishAllowanceMm),
        stepOverPct: num(v, 'stepOverPct', op.stepOverPct),
      }
    case 'drill': {
      const body = str(v, 'body', op.body) as DrillBody
      return {
        ...applyCommon(op, v),
        body,
        pattern: str(v, 'pattern', op.pattern) as DrillPattern,
        peckMm: num(v, 'peckMm', op.peckMm),
        hole: {
          x: num(v, 'hole.x', op.hole.x),
          y: num(v, 'hole.y', op.hole.y),
          r: num(v, 'hole.r', op.hole.r),
        },
        x0: num(v, 'x0', op.x0),
        y0: num(v, 'y0', op.y0),
        cols: int(v, 'cols', op.cols),
        rows: int(v, 'rows', op.rows),
        dx: num(v, 'dx', op.dx),
        dy: num(v, 'dy', op.dy),
        count: int(v, 'count', op.count),
        startDeg: num(v, 'startDeg', op.startDeg),
        arcDeg: num(v, 'arcDeg', op.arcDeg),
        fullTurn: on(v, 'fullTurn', op.fullTurn),
      }
    }
  }
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
 */
export const formProblems = (program: QuickProgram, op: Operation): Problem[] => {
  const checked = validate({ ...program, operations: [{ ...op, enabled: true }] })
  return checked.ok ? [] : checked.bad
}
