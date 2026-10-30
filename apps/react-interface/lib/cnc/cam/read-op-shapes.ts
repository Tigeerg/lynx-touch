// The shape keys of the new kinds of a `.qprog` file (PLAN-QUICKPROG, 6, 7.4), C3: contour, slot,
// chamfer, tap, probe.
//
// The table of kinds, the shape keys of every kind and the common keys live in `read-op.ts`; the
// classic kinds (drill, face, pocket, circle-pocket) are read by `read-op-classic.ts`. This module
// owns the arity and the field of every `op.*` key of its kinds: a new key is one branch here and
// one line in `serialize-write.ts`. Two kinds have a list: a contour takes pairs `x y` to the end of
// the line, a probe triples `x y z`; the rest of the keys keep their fixed arity.
import type { DrillGeometry, Operation, TapOperation } from './ops'
import { arity, num, onOff } from './read-op-words'
import { QprogError } from './qprog-error'
import { readClassicShapeKey, readCounterboreR, TAP_LAYOUT_KEYS } from './read-op-classic'

/** The profile of a contour: pairs of `x y` on one line, at least one pair and an even count. */
function contourPoints(words: string[], where: string): { x: number; y: number; z: number }[] {
  if (words.length < 2 || words.length % 2 !== 0) {
    throw new QprogError(0, `${where}op.points ждёт пары 'x y', а получило ${String(words.length)} знач.`)
  }
  const points: { x: number; y: number; z: number }[] = []
  for (let i = 0; i + 1 < words.length; i += 2) {
    points.push({ x: num(words, i, 'points', where), y: num(words, i + 1, 'points', where), z: 0 })
  }
  return points
}

/** The `stepOver` of a slot: the same key the face and both pockets carry (PLAN-QUICKPROG, 7.6). */
const slotStep = (op: Extract<Operation, { kind: 'slot' }>, words: string[], key: string, where: string): void => {
  arity(words, 1, key, where)
  op.stepOverPct = num(words, 1, key, where)
}

/** The `from`/`to` of a slot or a chamfer: a point `x y z`; the plane of the cut is the operation's top. */
function walkPoint(op: Operation, sub: string, words: string[], key: string, where: string): void {
  arity(words, 3, key, where)
  const point = { x: num(words, 1, key, where), y: num(words, 2, key, where), z: num(words, 3, key, where) }
  if (op.kind !== 'slot' && op.kind !== 'chamfer') return
  if (sub === 'from') op.from = point
  else op.to = point
}

/** A contour key: `points`, `closed`, `allowance`, `side` (PLAN-QUICKPROG, 7.6). */
function contourKey(
  op: Extract<Operation, { kind: 'contour' }>,
  sub: string,
  words: string[],
  key: string,
  where: string,
): void {
  if (sub === 'points') op.points = contourPoints(words.slice(1), where)
  else if (sub === 'closed') {
    arity(words, 1, key, where)
    op.closed = onOff(words, key, where)
  } else if (sub === 'allowance') {
    arity(words, 1, key, where)
    op.allowanceMm = num(words, 1, key, where)
  } else if (sub === 'side') {
    arity(words, 1, key, where)
    if (words[1] !== 'material' && words[1] !== 'air') throw new QprogError(0, `${where}op.side ждёт material или air.`)
    op.side = words[1]
  }
}

/** A tap operation, whose layout keys write the drilling fields the same way. */
type Tap = Extract<Operation, { kind: 'tap' }>

/** The layout word of a tap: the same three patterns and the words of a `.qprog` op line. */
const TAP_WORDS: Record<string, DrillGeometry['pattern']> = { hole: 'hole', grid: 'grid', circle: 'circle' }

/** The layout keys of a tap, shared with the drilling reader by name (`hole`, `grid`, ...). */
function tapLayout(tap: Tap, sub: string, words: string[], key: string, where: string): void {
  arity(words, 1, key, where)
  const pattern = words[1] === undefined ? undefined : TAP_WORDS[words[1]]
  if (pattern === undefined) throw new QprogError(0, `${where}op.pattern ждёт hole, grid или circle.`)
  tap.pattern = pattern
}

/**
 * The touch points of a probe: triples `x y z` on one line, at least one triple and a multiple of
 * three. The same list style as `op.points` of a contour, with the Z of every touch.
 */
function probePoints(words: string[], where: string): { x: number; y: number; z: number }[] {
  if (words.length < 3 || words.length % 3 !== 0) {
    throw new QprogError(0, `${where}op.points ждёт тройки 'x y z', а получило ${String(words.length)} знач.`)
  }
  const points: { x: number; y: number; z: number }[] = []
  for (let i = 0; i + 2 < words.length; i += 3) {
    points.push({
      x: num(words, i, 'points', where),
      y: num(words, i + 1, 'points', where),
      z: num(words, i + 2, 'points', where),
    })
  }
  return points
}

/**
 * Reads one shape key of a new kind into `op`. `false` when the key is not of this module, so the
 * other shape reader of `read-op-classic.ts` may try it.
 */
export function readNewShapeKey(op: Operation, sub: string, words: string[], key: string, where: string): boolean {
  if (op.kind === 'contour') {
    contourKey(op, sub, words, key, where)
    return true
  }
  if (op.kind === 'probe') return probeKey(op, sub, words, key, where)
  if (sub === 'stepOver' && op.kind === 'slot') {
    slotStep(op, words, key, where)
  } else if ((sub === 'from' || sub === 'to') && (op.kind === 'slot' || op.kind === 'chamfer')) {
    walkPoint(op, sub, words, key, where)
  } else if (sub === 'width' && op.kind === 'slot') {
    arity(words, 1, key, where)
    op.widthMm = num(words, 1, key, where)
  } else if (sub === 'pattern' && op.kind === 'tap') {
    tapLayout(op, sub, words, key, where)
  } else if (sub === 'pattern' && op.kind === 'counterbore') {
    arity(words, 1, key, where)
    const pattern = words[1] === undefined ? undefined : TAP_WORDS[words[1]]
    if (pattern === undefined) throw new QprogError(0, `${where}op.pattern ждёт hole, grid или circle.`)
    op.pattern = pattern
  } else if (sub === 'pitch' && (op.kind === 'tap' || op.kind === 'thread')) {
    arity(words, 1, key, where)
    op.pitchMm = num(words, 1, key, where)
  } else if (op.kind === 'thread') {
    return threadKey(op, sub, words, key, where)
  } else if (sub === 'chamfer' && op.kind === 'chamfer') {
    arity(words, 1, key, where)
    op.chamferMm = num(words, 1, key, where)
  } else if (op.kind === 'counterbore' && COUNTERBORE_KEYS.includes(sub)) {
    readCounterboreKey(op, sub, words, key, where)
  } else if (op.kind === 'counterbore' && TAP_LAYOUT_KEYS.includes(sub)) {
    readClassicShapeKey(op, sub, words, key, where)
  } else if (op.kind === 'tap' && TAP_LAYOUT_KEYS.includes(sub)) {
    // The layout keys of a tap are the drilling ones: the classic reader writes the same fields.
    readClassicShapeKey(op, sub, words, key, where)
  } else {
    return false
  }
  return true
}

/** The shape keys of a counterbore beyond the drilling layout: the seat, its depth and the dwell. */
export const COUNTERBORE_KEYS: readonly string[] = ['seat', 'seatDepth', 'dwell']

/** A counterbore key: `seat`, `seatDepth`, `dwell` (the `r` is read by `read-op.ts`). */
function readCounterboreKey(
  op: Extract<Operation, { kind: 'counterbore' }>,
  sub: string,
  words: string[],
  key: string,
  where: string,
): void {
  arity(words, 1, key, where)
  const value = num(words, 1, key, where)
  if (sub === 'seat') op.seatDiameterMm = value
  else if (sub === 'seatDepth') op.seatDepthMm = value
  else op.dwellSec = value
}

/** A thread key: `hole`, `internal`, `threadDepth`, `allowance`, `stepOver` (the pitch above). */
function threadKey(
  op: Extract<Operation, { kind: 'thread' }>,
  sub: string,
  words: string[],
  key: string,
  where: string,
): boolean {
  switch (sub) {
    case 'hole':
      arity(words, 3, key, where)
      op.hole = { x: num(words, 1, key, where), y: num(words, 2, key, where), r: num(words, 3, key, where) }
      return true
    case 'internal':
      arity(words, 1, key, where)
      op.internal = onOff(words, key, where)
      return true
    case 'threadDepth':
      arity(words, 1, key, where)
      op.threadDepthMm = num(words, 1, key, where)
      return true
    case 'allowance':
      arity(words, 1, key, where)
      op.threadAllowanceMm = num(words, 1, key, where)
      return true
    case 'stepOver':
      arity(words, 1, key, where)
      op.stepOverPct = num(words, 1, key, where)
      return true
    default:
      return false
  }
}

/** A probe key: `points`, `range`, `touch`, `axis` (PLAN-QUICKPROG, 7.6). */
function probeKey(
  op: Extract<Operation, { kind: 'probe' }>,
  sub: string,
  words: string[],
  key: string,
  where: string,
): boolean {
  if (sub === 'points') op.points = probePoints(words.slice(1), where)
  else if (sub === 'range') {
    arity(words, 1, key, where)
    op.rangeMm = num(words, 1, key, where)
  } else if (sub === 'touch') {
    arity(words, 1, key, where)
    op.touchFeedMmMin = num(words, 1, key, where)
  } else if (sub === 'axis') {
    arity(words, 1, key, where)
    if (words[1] !== 'X' && words[1] !== 'Y' && words[1] !== 'Z') {
      throw new QprogError(0, `${where}op.axis ждёт X, Y или Z.`)
    }
    op.axis = words[1]
  } else {
    return false
  }
  return true
}
