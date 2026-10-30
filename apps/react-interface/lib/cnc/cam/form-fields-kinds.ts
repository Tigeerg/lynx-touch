// The fields of one operation of a kind, as the form holds them: strings, as the input fields do
// (PLAN-QUICKPROG, 6 and 7.4).
//
// The mechanics (the `num`/`kept`/`int`/`str`/`on` readers and the common fields) live in
// `form-fields.ts`; the other direction, the values of the kind, is `form-values.ts`. Here is the
// switch values -> operation. The narrow switch is what makes a forgotten kind a compile error.
// Shape fields are named the same in the document and the form (`rect.x`, `circle.r`).

import type { DrillPattern, Operation } from './ops'
import type { CommonFields } from './form-fields'
import { kept, int, str, on } from './form-fields'
import { pointsOf } from './contour-points'
import { customWithText } from './form-lines'
import { probeAxis, probePointsOf } from './probe-points'
import { counterboreFields, threadFields } from './form-fields-q5'
import { engraveFields, islandFields } from './form-fields-q5a'

/** The shape values of the kind; the narrow switch is what makes a forgotten kind a compile error. */
/**
 * The typed fields of the kind written over the common fields of the operation; the narrow switch
 * covers every kind. `common` carries the kind too, so each arm swaps it for its own literal and
 * the union narrows. A `points` text that is not a profile keeps the document vertices.
 */
export function fieldsOfShape(op: Operation, v: Record<string, string>, common: CommonFields): Operation {
  switch (op.kind) {
    case 'face':
      return {
        ...common,
        kind: 'face',
        z: kept(v, 'z', op.z),
        stepOverPct: kept(v, 'stepOverPct', op.stepOverPct),
      }
    case 'pocket':
      return {
        ...common,
        kind: 'pocket',
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
        ...common,
        kind: 'circle-pocket',
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
        ...common,
        kind: 'contour',
        // A list that is not a profile keeps the document vertices: the form names the field, as it
        // does for a number that is not one, and the operation stays plannable.
        points: pointsOf(v, 'points', op.points, kept(v, 'topZ', op.topZ)) ?? op.points,
        closed: on(v, 'closed', op.closed),
        allowanceMm: kept(v, 'allowanceMm', op.allowanceMm),
        side: str(v, 'side', op.side) === 'air' ? 'air' : 'material',
      }
    case 'slot':
      return {
        ...common,
        kind: 'slot',
        from: { ...op.from, x: kept(v, 'from.x', op.from.x), y: kept(v, 'from.y', op.from.y) },
        to: { ...op.to, x: kept(v, 'to.x', op.to.x), y: kept(v, 'to.y', op.to.y) },
        widthMm: kept(v, 'widthMm', op.widthMm),
        stepOverPct: kept(v, 'stepOverPct', op.stepOverPct),
      }
    case 'chamfer':
      return {
        ...common,
        kind: 'chamfer',
        from: { ...op.from, x: kept(v, 'from.x', op.from.x), y: kept(v, 'from.y', op.from.y) },
        to: { ...op.to, x: kept(v, 'to.x', op.to.x), y: kept(v, 'to.y', op.to.y) },
        chamferMm: kept(v, 'chamferMm', op.chamferMm),
      }
    case 'tap':
      return {
        ...op,
        ...common,
        pattern: str(v, 'pattern', op.pattern) as DrillPattern,
        pitchMm: kept(v, 'pitchMm', op.pitchMm),
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
    case 'counterbore':
      return counterboreFields(op, v, common)
    case 'thread':
      return threadFields(op, v, common)
    case 'engrave':
      return engraveFields(op, v, common)
    case 'pocket-island':
      return islandFields(op, v, common)
    case 'custom': {
      // The text of the field is split back into the lines of the block; an absent field keeps the
      // document lines, so a form that never touched the text does not wipe the block.
      const next = customWithText(op, v.gcode)
      return { ...common, kind: 'custom', gcode: next.gcode }
    }
    case 'probe':
      return {
        ...common,
        kind: 'probe',
        // A list that is not triples keeps the document points, as the contour keeps its vertices.
        points: probePointsOf(v, 'points', op.points) ?? op.points,
        rangeMm: kept(v, 'rangeMm', op.rangeMm),
        touchFeedMmMin: kept(v, 'touchFeedMmMin', op.touchFeedMmMin),
        axis: probeAxis(v, 'axis', op.axis),
      }
    case 'drill':
      return {
        ...op,
        ...common,
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
