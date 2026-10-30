// The pocket with an island (PLAN-QUICKPROG 7.6): the island stands inside the pocket.
//
// The pocket has to be a real rectangle, the island too, and the island has to stand inside the pocket
// with a clearance of at least the mill diameter between the two: a narrower gap cannot be cut, the
// mill would cut the island wall and the pocket wall in one pass. The island outside the pocket is a
// refusal of its own, because its wall is then not the pocket's wall.
import { fmt } from './feeds'
import type { IslandPocketOperation, Rect } from './ops'
import type { Problem } from './validate-common'
import { EPS, add } from './validate-common'

/** The clearance of the island to the pocket wall, mm: the mill has to pass between them. */
export const MIN_ISLAND_CLEARANCE_MM = 0

/** True when every side of `inner` stands inside `outer` (touching counts as inside). */
const inside = (inner: Rect, outer: Rect): boolean =>
  inner.x >= outer.x - EPS &&
  inner.y >= outer.y - EPS &&
  inner.x + inner.w <= outer.x + outer.w + EPS &&
  inner.y + inner.h <= outer.y + outer.h + EPS

/** The island rules of the pocket: the two rectangles, the island inside and the clearance. */
export function checkIslandPocket(op: IslandPocketOperation): Problem[] {
  const out: Problem[] = []
  if (!(op.rect.w > 0) || !(op.rect.h > 0)) {
    add(out, 'rect', 'Карман вырожден: ширина или высота не больше нуля.')
    return out
  }
  if (!(op.island.w > 0) || !(op.island.h > 0)) {
    add(out, 'island', 'Остров вырожден: ширина или высота не больше нуля.')
    return out
  }
  if (op.islandAllowanceMm < 0) {
    add(out, 'islandAllowance', `Припуск ${fmt(op.islandAllowanceMm)} мм отрицательный: фреза зайдёт в остров.`)
    return out
  }
  // The island outside the pocket is the clearance rule of its overlapping side: an island larger
  // than the pocket does not fit between the walls either, and the operator reads one wording.
  if (!inside(op.island, op.rect)) {
    if (gapOf(op) < op.tool.diameterMm - EPS) {
      add(out, 'island', gapReason(op))
    } else {
      add(
        out,
        'island',
        `Остров (${fmt(op.island.x)}, ${fmt(op.island.y)}, ${fmt(op.island.w)}x${fmt(op.island.h)} мм) выходит за ` +
          `карман (${fmt(op.rect.x)}, ${fmt(op.rect.y)}, ${fmt(op.rect.w)}x${fmt(op.rect.h)} мм): подвиньте остров внутрь.`,
      )
    }
    return out
  }
  // The narrowest gap between the two rectangles; the mill has to pass with the allowance.
  const minGap = Math.min(
    op.island.x - op.rect.x,
    op.rect.x + op.rect.w - (op.island.x + op.island.w),
    op.island.y - op.rect.y,
    op.rect.y + op.rect.h - (op.island.y + op.island.h),
  )
  const needed = op.tool.diameterMm + MIN_ISLAND_CLEARANCE_MM
  if (minGap < needed - EPS) add(out, 'island', gapReason(op))
  return out
}

/** The narrowest gap between the island and the pocket wall, mm; negative when they overlap. */
const gapOf = (op: IslandPocketOperation): number =>
  Math.min(
    op.island.x - op.rect.x,
    op.rect.x + op.rect.w - (op.island.x + op.island.w),
    op.island.y - op.rect.y,
    op.rect.y + op.rect.h - (op.island.y + op.island.h),
  )

/** The one wording of the clearance rule, shared by the inside and the outside case. */
const gapReason = (op: IslandPocketOperation): string =>
  `Остров не помещается: зазор меньше фрезы. Зазор ${fmt(gapOf(op))} мм, фреза ф${fmt(op.tool.diameterMm)} мм: ` +
  'нужно не меньше диаметра фрезы между островом и стенкой.'

/** The warnings of the island: none yet, the clearance is an error; kept for the card. */
export function islandWarnings(_op: IslandPocketOperation): Problem[] {
  return []
}
