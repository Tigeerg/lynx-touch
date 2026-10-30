// The shape values of one operation of a kind, as the form holds them (PLAN-QUICKPROG 6 and 7.4).
//
// The other direction, `fieldsOfShape` (values -> operation), lives in `form-fields-kinds.ts` with
// the mechanics of the form; the two are cut apart only because a switch over every kind of the
// union is long, and a file over the size guide hides the kind and the mechanics together. The
// switch is still exhaustive: a forgotten kind is a compile error.
import type { Operation } from './ops'
import { pointsText } from './contour-points'
import { linesText } from './form-lines'
import { probePointsText } from './probe-points'
import { counterboreValues, threadValues } from './form-fields-q5'
import { engraveValues, islandValues } from './form-fields-q5a'

export function valuesOfShape(op: Operation): Record<string, string> {
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
    case 'tap':
      return {
        pattern: op.pattern,
        pitchMm: String(op.pitchMm),
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
      }
    case 'counterbore':
      return counterboreValues(op)
    case 'thread':
      return threadValues(op)
    case 'engrave':
      return engraveValues(op)
    case 'pocket-island':
      return islandValues(op)
    case 'custom':
      return { gcode: linesText(op.gcode) }
    case 'probe':
      return {
        points: probePointsText(op.points),
        rangeMm: String(op.rangeMm),
        touchFeedMmMin: String(op.touchFeedMmMin),
        axis: op.axis,
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
