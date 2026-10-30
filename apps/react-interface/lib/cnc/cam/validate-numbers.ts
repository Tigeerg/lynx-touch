// Every number the common checks compare, and the table row the checks of the numbers use.
//
// The table lives apart from the rules of `validate-common.ts` so the rules read short: the fields
// name the input the operator typed into, and a number that is not a number is refused before the
// comparison rules (`x <= 0`), which see `NaN` as false, could let it through.
import type { Feeds } from './feeds'
import { feedsForDrill, feedsForMill } from './feeds'
import type { Operation } from './ops'

/** A field the number comes from, and the field the problem is bound to when the numbers are checked. */
export interface FieldSource {
  /** The problem field: `stepOver` for the shape rule, as `validate-shape.ts` reports it. */
  field: string
  /** The form field: `stepOverPct`, `rect.w`, `tool.diameterMm`, ... */
  form: string
  /** The words of the value for the reason: `глубина`, `шаг растра`, ... */
  label: string
}

/** One number of the document and the words the form shows for it. */
export interface NumberField extends FieldSource {
  value: number
}

/** The table row an operation is rated by: the drill row, or the flat mill row of its material. */
export const tableRowFor = (op: Operation): Feeds =>
  op.kind === 'drill'
    ? feedsForDrill(op.material, op.body, op.tool.diameterMm)
    : feedsForMill(op.material, op.tool.diameterMm)

/**
 * Every number the common checks compare, with the field the operator typed it into. The comparison
 * rules (`x <= 0`) are false for `NaN`, so a half-typed field would pass them and reach the planner;
 * the field is named here instead. The names are the problem fields of the other checks:
 * `stepOver` for the shape rule of `validate-shape.ts`, `geometry` for a coordinate.
 */
export function numberFields(op: Operation): NumberField[] {
  const field = (value: number, name: string, form: string, label: string): NumberField => ({
    value,
    field: name,
    form,
    label,
  })
  const common: NumberField[] = [
    field(op.tool.number, 'tool.number', 'tool.number', 'номер инструмента'),
    field(op.tool.diameterMm, 'tool.diameterMm', 'tool.diameterMm', 'диаметр инструмента'),
    field(op.tool.hNumber, 'tool.hNumber', 'tool.hNumber', 'номер коррекции H'),
    field(op.topZ, 'topZ', 'topZ', 'верх заготовки'),
    field(op.depthMm, 'depthMm', 'depthMm', 'глубина'),
    field(op.stepDownMm, 'stepDownMm', 'stepDownMm', 'шаг врезания'),
    field(op.cutting.spindleRpm, 'cutting.spindleRpm', 'cutting.spindleRpm', 'обороты'),
    field(op.cutting.feedMmMin, 'cutting.feedMmMin', 'cutting.feedMmMin', 'подача'),
    field(op.cutting.plungeFeedMmMin, 'cutting.plungeFeedMmMin', 'cutting.plungeFeedMmMin', 'подача врезания'),
  ]
  switch (op.kind) {
    case 'face':
      return [
        ...common,
        field(op.z, 'geometry', 'z', 'плоскость'),
        field(op.stepOverPct, 'stepOver', 'stepOverPct', 'перекрытие полос'),
      ]
    case 'pocket':
      return [
        ...common,
        field(op.rect.x, 'geometry', 'rect.x', 'X кармана'),
        field(op.rect.y, 'geometry', 'rect.y', 'Y кармана'),
        field(op.rect.w, 'geometry', 'rect.w', 'ширина кармана'),
        field(op.rect.h, 'geometry', 'rect.h', 'высота кармана'),
        field(op.cornerRadiusMm, 'geometry', 'cornerRadiusMm', 'радиус угла'),
        field(op.finishAllowanceMm, 'geometry', 'finishAllowanceMm', 'припуск'),
        field(op.stepOverPct, 'stepOver', 'stepOverPct', 'шаг растра'),
      ]
    case 'circle-pocket':
      return [
        ...common,
        field(op.circle.x, 'geometry', 'circle.x', 'X центра'),
        field(op.circle.y, 'geometry', 'circle.y', 'Y центра'),
        field(op.circle.r, 'geometry', 'circle.r', 'радиус кармана'),
        field(op.finishAllowanceMm, 'geometry', 'finishAllowanceMm', 'припуск'),
        field(op.stepOverPct, 'stepOver', 'stepOverPct', 'шаг растра'),
      ]
    case 'contour':
      // The points are checked by the form (`pointsOf` in `form-fields.ts`): a point that is not
      // two numbers is named there, before an operation with `NaN` vertices exists.
      return [...common, field(op.allowanceMm, 'points', 'allowanceMm', 'припуск')]
    case 'slot':
      return [
        ...common,
        field(op.from.x, 'from', 'from.x', 'X начала паза'),
        field(op.from.y, 'from', 'from.y', 'Y начала паза'),
        field(op.to.x, 'to', 'to.x', 'X конца паза'),
        field(op.to.y, 'to', 'to.y', 'Y конца паза'),
        field(op.widthMm, 'widthMm', 'widthMm', 'ширина паза'),
        field(op.stepOverPct, 'stepOver', 'stepOverPct', 'шаг проходов'),
      ]
    case 'chamfer':
      return [
        ...common,
        field(op.from.x, 'from', 'from.x', 'X начала кромки'),
        field(op.from.y, 'from', 'from.y', 'Y начала кромки'),
        field(op.to.x, 'to', 'to.x', 'X конца кромки'),
        field(op.to.y, 'to', 'to.y', 'Y конца кромки'),
        field(op.chamferMm, 'chamferMm', 'chamferMm', 'фаска'),
      ]
    case 'drill':
      return [
        ...common,
        field(op.hole.x, 'geometry', 'hole.x', 'X отверстия'),
        field(op.hole.y, 'geometry', 'hole.y', 'Y отверстия'),
        field(op.hole.r, 'geometry', 'hole.r', 'радиус отверстия'),
        field(op.x0, 'geometry', 'x0', 'X первого отверстия'),
        field(op.y0, 'geometry', 'y0', 'Y первого отверстия'),
        field(op.cols, 'geometry', 'cols', 'число столбцов'),
        field(op.rows, 'geometry', 'rows', 'число строк'),
        field(op.dx, 'geometry', 'dx', 'шаг по X'),
        field(op.dy, 'geometry', 'dy', 'шаг по Y'),
        field(op.count, 'geometry', 'count', 'число отверстий'),
        field(op.startDeg, 'geometry', 'startDeg', 'начальный угол'),
        field(op.arcDeg, 'geometry', 'arcDeg', 'угол дуги'),
        field(op.peckMm, 'peck', 'peckMm', 'шаг вывода стружки'),
      ]
    case 'tap':
      return [
        ...common,
        field(op.pitchMm, 'pitchMm', 'pitchMm', 'шаг резьбы'),
        field(op.hole.x, 'geometry', 'hole.x', 'X отверстия'),
        field(op.hole.y, 'geometry', 'hole.y', 'Y отверстия'),
        field(op.hole.r, 'geometry', 'hole.r', 'радиус резьбы'),
        field(op.x0, 'geometry', 'x0', 'X первого отверстия'),
        field(op.y0, 'geometry', 'y0', 'Y первого отверстия'),
        field(op.cols, 'geometry', 'cols', 'число столбцов'),
        field(op.rows, 'geometry', 'rows', 'число строк'),
        field(op.dx, 'dx', 'dx', 'шаг по X'),
        field(op.dy, 'dy', 'dy', 'шаг по Y'),
        field(op.count, 'geometry', 'count', 'число отверстий'),
        field(op.startDeg, 'geometry', 'startDeg', 'начальный угол'),
        field(op.arcDeg, 'geometry', 'arcDeg', 'угол дуги'),
      ]
    case 'custom':
      // The lines are text, not numbers: the empty list is the only rule (`validate-custom.ts`) and
      // the viewer parser reads the words (`viewer-check.ts`).
      return [...common]
    case 'probe':
      // The points are a list, read by the form (`probe-points.ts`); the numbers here are the frame
      // of the touch: the range of the search and the touch feed.
      return [
        ...common,
        field(op.rangeMm, 'rangeMm', 'rangeMm', 'диапазон обмера'),
        field(op.touchFeedMmMin, 'touchFeedMmMin', 'touchFeedMmMin', 'подача касания'),
      ]
  }
}
