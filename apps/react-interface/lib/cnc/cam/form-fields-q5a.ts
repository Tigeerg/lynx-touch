// The shape values of the engraving and of the island pocket (PLAN-QUICKPROG 7.6) in both
// directions of the form.
//
// The switch of the form (`form-fields-kinds.ts`) calls these four functions, so the text of a label
// and the two rectangles of a pocket sit in a module of their own and the switch stays under the
// size guide. The readers are the ones of `form-fields.ts`: a field that is not a number keeps the
// model value, a text is taken as typed.
import type { Operation } from './ops'
import type { CommonFields } from './form-fields'
import { kept, str } from './form-fields'

/** An engraving: the text, its point and the frame of the string. */
type Engrave = Extract<Operation, { kind: 'engrave' }>
type Island = Extract<Operation, { kind: 'pocket-island' }>

/** The shape values of an engraving, as the form shows them. */
export function engraveValues(op: Engrave): Record<string, string> {
  return {
    text: op.text,
    x: String(op.x),
    y: String(op.y),
    heightMm: String(op.heightMm),
    angleDeg: String(op.angleDeg),
  }
}

/** The shape values of an island pocket: the two rectangles and the two numbers of the raster. */
export function islandValues(op: Island): Record<string, string> {
  return {
    'rect.x': String(op.rect.x),
    'rect.y': String(op.rect.y),
    'rect.w': String(op.rect.w),
    'rect.h': String(op.rect.h),
    'island.x': String(op.island.x),
    'island.y': String(op.island.y),
    'island.w': String(op.island.w),
    'island.h': String(op.island.h),
    islandAllowanceMm: String(op.islandAllowanceMm),
    stepOverPct: String(op.stepOverPct),
  }
}

/** The typed fields of an engraving; an empty text stays empty for the check to name it. */
export function engraveFields(op: Engrave, v: Record<string, string>, common: CommonFields): Engrave {
  return {
    ...common,
    kind: 'engrave',
    text: str(v, 'text', op.text),
    x: kept(v, 'x', op.x),
    y: kept(v, 'y', op.y),
    heightMm: kept(v, 'heightMm', op.heightMm),
    angleDeg: kept(v, 'angleDeg', op.angleDeg),
  }
}

/** The typed fields of an island pocket. */
export function islandFields(op: Island, v: Record<string, string>, common: CommonFields): Island {
  return {
    ...common,
    kind: 'pocket-island',
    rect: {
      x: kept(v, 'rect.x', op.rect.x),
      y: kept(v, 'rect.y', op.rect.y),
      w: kept(v, 'rect.w', op.rect.w),
      h: kept(v, 'rect.h', op.rect.h),
    },
    island: {
      x: kept(v, 'island.x', op.island.x),
      y: kept(v, 'island.y', op.island.y),
      w: kept(v, 'island.w', op.island.w),
      h: kept(v, 'island.h', op.island.h),
    },
    islandAllowanceMm: kept(v, 'islandAllowanceMm', op.islandAllowanceMm),
    stepOverPct: kept(v, 'stepOverPct', op.stepOverPct),
  }
}
