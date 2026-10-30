// The fields of one operation as the form holds them: strings, as the input fields do.
//
// `values` is what the operator typed, so a value that is not a number yet stays in the field
// instead of being silently replaced. The mapping to the operation is explicit in both directions:
//
//   operation -> values   `valuesOf`, so CAM_EDIT opens the form with the numbers of the document
//   values -> operation   `applyFields`, fields grouped by meaning
//
// A new form starts from `defaults.ts`; shape fields are named the same in the document and the
// form (`rect.x`, `circle.r`). A contour has one text field for its points, `X Y; X Y; ...`.
import type { DrillBody, Material } from './feeds'
import { cuttingFor } from './feeds'
import type { DrillPattern, Operation, OperationKind, QuickProgram } from './ops'
import { stockTop } from './ops'
import { newOperation } from './defaults'
import type { Problem } from './validate'
import { pointsOf, pointsText } from './contour-points'

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

/**
 * The typed values of an operation: the common fields and the ones of its own kind. The narrow
 * switch is what makes a forgotten kind a compile error.
 */
export function valuesOf(op: Operation): Record<string, string> {
  return { ...baseValues(op), ...valuesOfShape(op) }
}

/** The shape fields of the kind, by the narrow switch over `kind`. */
function valuesOfShape(op: Operation): Record<string, string> {
  switch (op.kind) {
    case 'face':
      return { z: String(op.z), stepOverPct: String(op.stepOverPct) }
    case 'pocket':
      return {
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
        'circle.x': String(op.circle.x),
        'circle.y': String(op.circle.y),
        'circle.r': String(op.circle.r),
        finishAllowanceMm: String(op.finishAllowanceMm),
        stepOverPct: String(op.stepOverPct),
      }
    case 'contour':
      return {
        points: pointsText(op.points),
        closed: op.closed ? 'on' : 'off',
        allowanceMm: String(op.allowanceMm),
        side: op.side,
      }
    case 'slot':
      return {
        'from.x': String(op.from.x),
        'from.y': String(op.from.y),
        'to.x': String(op.to.x),
        'to.y': String(op.to.y),
        widthMm: String(op.widthMm),
        stepOverPct: String(op.stepOverPct),
      }
    case 'chamfer':
      return {
        'from.x': String(op.from.x),
        'from.y': String(op.from.y),
        'to.x': String(op.to.x),
        'to.y': String(op.to.y),
        chamferMm: String(op.chamferMm),
      }
    case 'drill':
      return {
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

/** Writes the typed fields of the kind onto the operation; the narrow switch covers every kind. */
export function applyFields(op: Operation, v: Record<string, string>): Operation {
  switch (op.kind) {
    case 'face':
      return {
        ...applyCommon(op, v),
        z: kept(v, 'z', op.z),
        stepOverPct: kept(v, 'stepOverPct', op.stepOverPct),
      }
    case 'pocket':
      return {
        ...applyCommon(op, v),
        rect: {
          x: kept(v, 'rect.x', op.rect.x),
          y: kept(v, 'rect.y', op.rect.y),
          w: kept(v, 'rect.w', op.rect.w),
          h: kept(v, 'rect.h', op.rect.h),
        },
        cornerRadiusMm: kept(v, 'cornerRadiusMm', op.cornerRadiusMm),
        finishAllowanceMm: kept(v, 'finishAllowanceMm', op.finishAllowanceMm),
        stepOverPct: kept(v, 'stepOverPct', op.stepOverPct),
      }
    case 'circle-pocket':
      return {
        ...applyCommon(op, v),
        circle: {
          x: kept(v, 'circle.x', op.circle.x),
          y: kept(v, 'circle.y', op.circle.y),
          r: kept(v, 'circle.r', op.circle.r),
        },
        finishAllowanceMm: kept(v, 'finishAllowanceMm', op.finishAllowanceMm),
        stepOverPct: kept(v, 'stepOverPct', op.stepOverPct),
      }
    case 'contour':
      return {
        ...applyCommon(op, v),
        // A list that is not a profile keeps the document vertices: the form names the field, as it
        // does for a number that is not one, and the operation stays plannable.
        points: pointsOf(v, 'points', op.points, kept(v, 'topZ', op.topZ)) ?? op.points,
        closed: on(v, 'closed', op.closed),
        allowanceMm: kept(v, 'allowanceMm', op.allowanceMm),
        side: str(v, 'side', op.side) === 'air' ? 'air' : 'material',
      }
    case 'slot':
      return {
        ...applyCommon(op, v),
        from: { ...op.from, x: kept(v, 'from.x', op.from.x), y: kept(v, 'from.y', op.from.y) },
        to: { ...op.to, x: kept(v, 'to.x', op.to.x), y: kept(v, 'to.y', op.to.y) },
        widthMm: kept(v, 'widthMm', op.widthMm),
        stepOverPct: kept(v, 'stepOverPct', op.stepOverPct),
      }
    case 'chamfer':
      return {
        ...applyCommon(op, v),
        from: { ...op.from, x: kept(v, 'from.x', op.from.x), y: kept(v, 'from.y', op.from.y) },
        to: { ...op.to, x: kept(v, 'to.x', op.to.x), y: kept(v, 'to.y', op.to.y) },
        chamferMm: kept(v, 'chamferMm', op.chamferMm),
      }
    case 'drill': {
      const body = str(v, 'body', op.body) as DrillBody
      return {
        ...applyCommon(op, v),
        body,
        pattern: str(v, 'pattern', op.pattern) as DrillPattern,
        peckMm: kept(v, 'peckMm', op.peckMm),
        hole: {
          x: kept(v, 'hole.x', op.hole.x),
          y: kept(v, 'hole.y', op.hole.y),
          r: kept(v, 'hole.r', op.hole.r),
        },
        x0: kept(v, 'x0', op.x0),
        y0: kept(v, 'y0', op.y0),
        cols: int(v, 'cols', op.cols),
        rows: int(v, 'rows', op.rows),
        dx: kept(v, 'dx', op.dx),
        dy: kept(v, 'dy', op.dy),
        count: int(v, 'count', op.count),
        startDeg: kept(v, 'startDeg', op.startDeg),
        arcDeg: kept(v, 'arcDeg', op.arcDeg),
        fullTurn: on(v, 'fullTurn', op.fullTurn),
      }
    }
  }
}
