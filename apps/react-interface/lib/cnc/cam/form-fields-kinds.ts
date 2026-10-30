// The fields of one operation of a kind, as the form holds them: strings, as the input fields do
// (PLAN-QUICKPROG, 6 and 7.4).
//
// The mechanics (the `num`/`kept`/`int`/`str`/`on` readers, the common fields and the two switches
// that call this module) live in `form-fields.ts`. Here are the shape switches, one per direction:
// the values of the kind, and the kind with the typed fields written onto it. The narrow switch is
// what makes a forgotten kind a compile error. Shape fields are named the same in the document and
// the form (`rect.x`, `circle.r`); a contour has one text field, `X Y; X Y; ...`.

import type { DrillPattern, Operation } from './ops'
import type { CommonFields } from './form-fields'
import { kept, int, str, on } from './form-fields'
import { pointsOf, pointsText } from './contour-points'
import { customWithText, linesText } from './form-lines'
import { probeAxis, probePointsOf, probePointsText } from './probe-points'

/** The shape values of the kind; the narrow switch is what makes a forgotten kind a compile error. */
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
