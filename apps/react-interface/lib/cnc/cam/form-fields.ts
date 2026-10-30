// The fields of one operation as the form holds them: strings, as the input fields do.
//
// `values` is what the operator typed, so a value that is not a number yet stays in the field
// instead of being silently replaced. The mapping to the operation is explicit in both directions:
//
//   operation -> values   `valuesOf`, so CAM_EDIT opens the form with the numbers of the document
//   values -> operation   `applyFields`, fields grouped by meaning
//
// The mechanics live here: the readers of a typed value and the common fields. The shape fields of
// a kind are in `form-fields-kinds.ts`, which this module calls. A new form starts from
// `defaults.ts`; shape fields are named the same in the document and the form (`rect.x`,
// `circle.r`). A contour has one text field for its points, `X Y; X Y; ...`.
import type { DrillBody, Material } from './feeds'
import { cuttingFor } from './feeds'
import type { Operation, OperationKind, QuickProgram } from './ops'
import { stockTop } from './ops'
import { newOperation } from './defaults'
import type { Problem } from './validate'
import { fieldsOfShape } from './form-fields-kinds'
import { valuesOfShape } from './form-values'

/** The form of an operation: the kind, the typed values, and the result of the last edit. */
export interface CamForm {
  /** The kind of the operation the form builds. */
  op: OperationKind
  /** The typed values by field name: `depthMm`, `tool.number`, `rect.w`, ... */
  values: Record<string, string>
  /** The problems of the last edit, each bound to the field the operator sees. */
  error: Problem | null
}

// A field that is not a number yet (`'abc'`, `'-'`, `'1..2'`) is read as `NaN` here and never leaves
// these modules: the builder keeps the model value in its place (`kept`). The typed text stays in
// `form.values` for the operator to finish, and `formProblems` refuses it by name (`checkNumbers` in
// `validate-numbers.ts`). An empty field is not `NaN`: it is the value the form reads as "not typed".

/** A number from a typed value; `NaN` when the field is not a number, so the caller can skip it. */
export const num = (values: Record<string, string>, key: string, fallback: number): number => {
  const text = values[key]
  // `undefined` (a key of another kind of operation) and `''` (a cleared field) are "not typed":
  // the model value stands. The text the operator typed that parses to no number is `NaN`.
  if (text === undefined || text.trim() === '') return fallback
  const value = Number(text.replace(',', '.'))
  return Number.isFinite(value) ? value : Number.NaN
}

/** The number the operation takes: the typed one, or the model value when the field is not a number. */
export const kept = (values: Record<string, string>, key: string, fallback: number): number => {
  const value = num(values, key, fallback)
  return Number.isFinite(value) ? value : fallback
}

/** An integer from a typed value: the model value when the field is not a number, the truncation otherwise. */
export const int = (values: Record<string, string>, key: string, fallback: number): number =>
  Math.trunc(kept(values, key, fallback))

export const str = (values: Record<string, string>, key: string, fallback: string): string => values[key] ?? fallback

export const on = (values: Record<string, string>, key: string, fallback: boolean): boolean => {
  const text = values[key]
  if (text === undefined) return fallback
  return text === 'on' || text === 'true' || text === '1' || text === 'да'
}

const cuttingOf = (values: Record<string, string>, material: Material, diameterMm: number, body: DrillBody | null) => {
  const table = cuttingFor(material, diameterMm, body)
  return {
    spindleRpm: kept(values, 'cutting.spindleRpm', table.spindleRpm),
    feedMmMin: kept(values, 'cutting.feedMmMin', table.feedMmMin),
    plungeFeedMmMin: kept(values, 'cutting.plungeFeedMmMin', table.plungeFeedMmMin),
  }
}

/** The typed values of a new operation of `kind` over the top `topZ`; `newForm` starts from them. */
export const formValues = (kind: OperationKind, topZ: number): Record<string, string> =>
  valuesOf(newOperation(kind, { id: '', topZ }))

/** A new form of `kind`: the defaults of the operation over the stock top. */
export const newForm = (kind: OperationKind, program: QuickProgram): CamForm => ({
  op: kind,
  values: formValues(kind, stockTop(program.stock)),
  error: null,
})

/**
 * The base values of an operation. A custom block has no cut of its own and may name no tool: its
 * own lines are the program, so the mode fields stay in the form for the header tags only.
 */
const baseValues = (op: Operation): Record<string, string> => ({
  id: op.id,
  enabled: op.enabled ? 'on' : 'off',
  'tool.number': String(op.tool.number),
  'tool.diameterMm': String(op.tool.diameterMm),
  'tool.hNumber': String(op.tool.hNumber),
  material: op.material,
  coolant: op.coolant,
  'cutting.spindleRpm': String(op.cutting.spindleRpm),
  'cutting.feedMmMin': String(op.cutting.feedMmMin),
  'cutting.plungeFeedMmMin': String(op.cutting.plungeFeedMmMin),
  topZ: String(op.topZ),
  depthMm: String(op.depthMm),
  stepDownMm: String(op.stepDownMm),
  notes: op.notes,
})

/** The typed values of an operation: the common fields and the ones of its own kind. */
export function valuesOf(op: Operation): Record<string, string> {
  return { ...baseValues(op), ...valuesOfShape(op) }
}

/** The common fields from the typed values: the tool, the rates (the table fills an empty field), the frame. */
function applyCommon<T extends Operation>(op: T, values: Record<string, string>): T {
  const number = int(values, 'tool.number', op.tool.number)
  const diameterMm = kept(values, 'tool.diameterMm', op.tool.diameterMm)
  const material = str(values, 'material', op.material) as Material
  return {
    ...op,
    id: str(values, 'id', op.id),
    enabled: on(values, 'enabled', op.enabled),
    tool: { number, diameterMm, hNumber: int(values, 'tool.hNumber', number) },
    cutting: cuttingOf(values, material, diameterMm, op.kind === 'drill' ? op.body : null),
    coolant: str(values, 'coolant', op.coolant) as Operation['coolant'],
    material,
    topZ: kept(values, 'topZ', op.topZ),
    depthMm: kept(values, 'depthMm', op.depthMm),
    stepDownMm: kept(values, 'stepDownMm', op.stepDownMm),
    notes: str(values, 'notes', op.notes),
  }
}

/** The common fields of an operation, which the shape switch of the kind writes its own under. */
export type CommonFields = Pick<
  Operation,
  'id' | 'enabled' | 'tool' | 'cutting' | 'coolant' | 'material' | 'topZ' | 'depthMm' | 'stepDownMm' | 'notes'
>

/** Writes the typed fields of the kind onto the operation; the narrow switch covers every kind. */
export function applyFields(op: Operation, v: Record<string, string>): Operation {
  return fieldsOfShape(op, v, applyCommon(op, v))
}
