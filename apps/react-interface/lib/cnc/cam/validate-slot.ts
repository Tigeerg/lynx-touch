// The slot and chamfer rules (PLAN-QUICKPROG, 7.6): the geometry the mill can cut at all.
//
// Slot: the two ends make a line (a length under `MIN_SLOT_MM` is a point), the slot is not narrower
// than the mill (the mill cannot go in) and a width over two diameters is a warning, not an error:
// the document is still planable, but a contour cuts such a pocket cleanly. The warning is kept apart
// from the `validate` list, so the form shows it and the save does not refuse the file.
//
// Chamfer: the leg is positive and not above the mill radius (the corner of a flat mill can only take
// a chamfer up to its own radius). Only a straight edge is modelled; a slot of one segment has no arc,
// so the "arcs are a warning" rule of the plan has nothing to check here.
import { fmt } from './feeds'
import type { ChamferOperation, SlotOperation } from './ops'
import type { Problem } from './validate-common'
import { EPS, add } from './validate-common'

/** The shortest slot worth a pass, mm: shorter is a point, not a keyway. */
export const MIN_SLOT_MM = 1

/** The distance of the two ends in plan, mm; the `z` of an end is not the plane of the cut. */
const lengthMm = (op: SlotOperation | ChamferOperation): number => Math.hypot(op.to.x - op.from.x, op.to.y - op.from.y)

/** A slot: two ends, a width the mill fits and a step over the strips. Errors only. */
export function checkSlot(op: SlotOperation, out: Problem[]): void {
  if (lengthMm(op) < MIN_SLOT_MM - EPS) {
    add(out, 'to', `Длина паза меньше ${fmt(MIN_SLOT_MM)} мм: задайте две разные точки вдоль паза.`)
    return
  }
  if (op.widthMm < op.tool.diameterMm - EPS) {
    add(out, 'widthMm', 'Паз уже фрезы: возьмите фрезу не шире паза.')
    return
  }
  // The strip step is the pocket rule, named on the slot's own field so the form points at it.
  if (op.stepOverPct < 10 - EPS) {
    add(out, 'stepOver', `Шаг ${fmt(op.stepOverPct)} % меньше 10 % диаметра: фреза будет тереть уже срезанное.`)
  } else if (op.stepOverPct > 100 + EPS) {
    add(out, 'stepOver', `Шаг ${fmt(op.stepOverPct)} % больше 100 % диаметра: между проходами останутся дыры.`)
  }
}

/**
 * The slot warnings: what does not block the save but the operator should see. A slot wider than two
 * diameters cuts, but a contour does it in one clean loop; the form shows the reason next to the
 * width, the save goes on.
 */
export function slotWarnings(op: SlotOperation): Problem[] {
  const out: Problem[] = []
  if (op.widthMm > 2 * op.tool.diameterMm + EPS) {
    add(out, 'widthMm', 'Паз шире двух диаметров: точнее контуром.')
  }
  return out
}

/** A chamfer: a straight edge of a leg the corner of the mill can take. Errors only. */
export function checkChamfer(op: ChamferOperation, out: Problem[]): void {
  if (!(op.chamferMm > 0)) {
    add(out, 'chamferMm', 'Фаска должна быть больше нуля: иначе снимать нечего.')
    return
  }
  if (op.chamferMm > op.tool.diameterMm / 2 + EPS) {
    add(out, 'chamferMm', 'Фаска больше радиуса фрезы: возьмите фрезу крупнее или меньше фаску.')
    return
  }
  if (lengthMm(op) < MIN_SLOT_MM - EPS) {
    add(out, 'to', `Длина кромки меньше ${fmt(MIN_SLOT_MM)} мм: задайте две разные точки вдоль кромки.`)
  }
}
