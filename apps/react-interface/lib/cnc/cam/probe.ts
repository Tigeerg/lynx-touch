// The probe block (PLAN-QUICKPROG, 7.6, «Блок щупа»): G38.2 per touch point (PLAN-QUICKPROG, 7.6).
//
// The operator puts a measurement between the cutting operations: the probe touches the part along
// `axis` at the coordinate of each point and backs off by `rangeMm`. The tool is the probe of the
// tool table and reaches the operation through the common fields, so the emitter prints the same
// `UI_TOOL` and the same `M6` as for a mill or a drill; the tag family is its own (`PROBE`).
//
// The words of one measurement are `G38.2 <axis><coordinate> F<touch>`, the retreat is a rapid to
// the measured point raised by `rangeMm` along the same axis. The range is the travel of the touch
// search: on the Z axis it is above the point, on X and Y it is along the axis away from the part.
// The block cuts nothing, so the planner writes no move and the time estimate charges none.
import { fmt } from './feeds'
import type { ProbeAxis, ProbeOperation } from './ops'
import { coord, feedWord } from './emit-words'
import type { Problem } from './validate-common'
import { add } from './validate-common'

/** The coordinate of a point along the touch axis. */
const along = (point: { x: number; y: number; z: number }, axis: ProbeAxis): number =>
  point[axis.toLowerCase() as 'x' | 'y' | 'z']

/** The point raised by `rangeMm` along the axis: where the probe stands before the next touch. */
const retracted = (point: ProbeOperation['points'][number], axis: ProbeAxis, rangeMm: number) => {
  const raised = { ...point }
  raised[axis.toLowerCase() as 'x' | 'y' | 'z'] = along(point, axis) + rangeMm
  return raised
}

/**
 * The lines of the probe block: one `G38.2` and one retreat per point, in the order of the list.
 * An empty list is an error of the check, and the caller plans nothing.
 */
export function probeLines(op: ProbeOperation): string[] {
  const lines: string[] = []
  const feed = feedWord(op.touchFeedMmMin)
  for (const point of op.points) {
    lines.push(`G38.2 ${op.axis}${coord(along(point, op.axis))} F${feed}`)
    lines.push(`G0 ${op.axis}${coord(along(retracted(point, op.axis, op.rangeMm), op.axis))}`)
  }
  return lines
}

/**
 * The errors of a probe block as the form shows them: the empty list, a range that is not a
 * travel, a touch feed that is not a feed. The tool and the cutting data are checked as of every
 * kind (`validate-common.ts`); the shape rules of the probe are here, so the form and the save
 * refuse with the same words.
 */
export function probeValidation(op: ProbeOperation): Problem[] {
  const out: Problem[] = []
  if (op.points.length === 0) add(out, 'points', 'У щупа нет ни одной точки измерения: добавьте точку.')
  if (!(op.rangeMm > 0)) add(out, 'rangeMm', 'Диапазон обмера должен быть больше нуля: щуп не найдёт поверхность.')
  if (!(op.touchFeedMmMin > 0)) add(out, 'touchFeedMmMin', 'Подача касания должна быть больше нуля.')
  for (const point of op.points) {
    if (![point.x, point.y, point.z].every(Number.isFinite)) {
      add(out, 'points', 'Точка измерения задана не числом: введите X, Y и Z.')
      break
    }
  }
  return out
}

/**
 * The warnings of a probe block: a touch feed above the working feed may break the probe tip, but
 * the operator may know better than the document, so it is a warning and not a refusal.
 */
export function probeWarnings(op: ProbeOperation): Problem[] {
  const out: Problem[] = []
  if (op.touchFeedMmMin > op.cutting.feedMmMin && op.cutting.feedMmMin > 0) {
    add(
      out,
      'touchFeedMmMin',
      `Подача касания ${fmt(op.touchFeedMmMin)} мм/мин выше рабочей ${fmt(op.cutting.feedMmMin)}: щуп может сломаться.`,
    )
  }
  return out
}
