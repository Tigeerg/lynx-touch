// The engraving and the island pocket rules (PLAN-QUICKPROG 7.6): the text and the island fit.
//
// Both kinds are refused and warned from one module, so the shape switch of `validate-shape.ts` and
// the form read the same two functions and a kind is added here once.
import { engraveToolWarnings, checkEngrave } from './validate-engrave'
import { checkIslandPocket, islandWarnings } from './validate-island'
import type { EngraveOperation, IslandPocketOperation, Operation } from './ops'
import { threadWarnings } from './path-thread'
import { add } from './validate-common'
import type { Problem } from './validate-common'

/** The blocking rules of the two kinds; a kind of another sort is left to its own module. */
export function checkPathKind(op: Operation, out: Problem[]): void {
  if (op.kind === 'engrave') {
    checkEngrave(op, out)
    return
  }
  if (op.kind === 'pocket-island') {
    for (const item of checkIslandPocket(op)) add(out, item.field, item.reason)
  }
}

/** The warnings of the plan kinds: what the save keeps but the operator should read. */
export function pathWarnings(op: Operation): Problem[] {
  if (op.kind === 'engrave') return [...engraveToolWarnings(op), ...engraveHeight(op)]
  if (op.kind === 'pocket-island') return islandWarnings(op)
  // A milled thread climbs whole turns: the tail of the length stays uncut and is named here.
  if (op.kind === 'thread') return threadWarnings(op)
  return []
}

/** The height warning of an engraving, kept in one place with the tool one. */
const engraveHeight = (op: EngraveOperation): Problem[] => {
  if (op.heightMm > 0 && op.heightMm < 1) {
    return [{ field: 'heightMm', reason: 'Высота меньше 1 мм: надпись будет читаться только под лупой.' }]
  }
  return []
}

export type { EngraveOperation, IslandPocketOperation }
