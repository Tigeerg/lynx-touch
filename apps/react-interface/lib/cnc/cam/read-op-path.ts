// The shape keys of the engraving and the island pocket of a `.qprog` file (PLAN-QUICKPROG 7.6).
//
// `op engrave` keeps its string in `op.text` and its frame in `op.x`, `op.y`, `op.height` and
// `op.angle`; `op pocket-island` keeps the two rectangles in `op.rect` and `op.island`, the
// allowance and the step. The text is the rest of the line, quoted or not, as `op.notes` is; the
// numbers keep the arity of their key. `true` when the key was of this module, so the reader of the
// other shapes is not called with it.
import type { Operation } from './ops'
import { parseInteger, restAfter, unquoteText } from './serialize-words'
import { arity, num } from './read-op-words'
import { QprogError } from './qprog-error'

/** The rectangle of an island pocket: `x y w h` on one line, as a pocket's `op.rect` is. */
const rectOf = (words: string[], key: string, where: string) => {
  arity(words, 4, key, where)
  return {
    x: num(words, 1, key, where),
    y: num(words, 2, key, where),
    w: num(words, 3, key, where),
    h: num(words, 4, key, where),
  }
}

/** An engraving key: the text and the four numbers of the frame. */
function engraveKey(
  op: Extract<Operation, { kind: 'engrave' }>,
  sub: string,
  line: string,
  words: string[],
  key: string,
  where: string,
): void {
  if (sub === 'text') {
    op.text = words.length > 1 ? unquoteText(restAfter(line, 1)) : ''
    return
  }
  arity(words, 1, key, where)
  const value = num(words, 1, key, where)
  if (sub === 'x') op.x = value
  else if (sub === 'y') op.y = value
  else if (sub === 'height') op.heightMm = value
  else op.angleDeg = value
}

/** An island pocket key: the two rectangles, the allowance and the step. */
function islandKey(
  op: Extract<Operation, { kind: 'pocket-island' }>,
  sub: string,
  words: string[],
  key: string,
  where: string,
): void {
  if (sub === 'rect') op.rect = rectOf(words, key, where)
  else if (sub === 'island') op.island = rectOf(words, key, where)
  else {
    arity(words, 1, key, where)
    const value = num(words, 1, key, where)
    if (sub === 'islandAllowance') op.islandAllowanceMm = value
    else op.stepOverPct = value
  }
}

/**
 * A rest key: `source` is the list of document indexes as the rest of the line (empty is no
 * source), `rect` is the footprint of four numbers, `allowance` and `stepOver` are one number each.
 */
function restKey(
  op: Extract<Operation, { kind: 'rest' }>,
  sub: string,
  words: string[],
  key: string,
  where: string,
): void {
  if (sub === 'source') {
    const list: number[] = []
    for (const word of words.slice(1)) {
      const value = parseInteger(word)
      if (value === null || value < 0) throw new QprogError(0, `${where}op.source ждёт целые индексы: '${word}'.`)
      if (!list.includes(value)) list.push(value)
    }
    op.source = list
    return
  }
  if (sub === 'rect') {
    op.rect = rectOf(words, key, where)
    return
  }
  arity(words, 1, key, where)
  const value = num(words, 1, key, where)
  if (sub === 'allowance') op.allowanceMm = value
  else op.stepOverPct = value
}

/** Reads one shape key of the kinds; `false` when the key is of another kind. */
export function readPathShapeKey(
  op: Operation,
  sub: string,
  words: string[],
  key: string,
  where: string,
  line: string,
): boolean {
  if (op.kind === 'engrave') {
    engraveKey(op, sub, line, words, key, where)
    return true
  }
  if (op.kind === 'pocket-island') {
    islandKey(op, sub, words, key, where)
    return true
  }
  if (op.kind === 'rest') {
    restKey(op, sub, words, key, where)
    return true
  }
  return false
}
