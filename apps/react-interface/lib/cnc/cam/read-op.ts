// One operation of a `.qprog` file (PLAN-QUICKPROG, 6, 7.4), C3, the port of
// `serialize_read_op.cpp` and `qprog_operation.cpp`.
//
// This module owns the table of kinds and the common keys of an `op.*` key: a new common key is one
// branch here and one line in `serialize-write.ts`. The line loop is in `read-program.ts`; the shape
// keys of the classic kinds (drill, face, pocket, circle-pocket) are in `read-op-classic.ts`, of the
// new ones (contour, slot, chamfer) in `read-op-shapes.ts`, and the small lexer they share in
// `read-op-words.ts`. This file is the facade: it re-exports the tables the reader loop needs.
import type { Operation, QuickProgram } from './ops'
import { COMMON_KEYS } from './read-header'
import { parseInteger, restAfter, unquoteText } from './serialize-words'
import { arity, num, onOff } from './read-op-words'
import { DRILL_KEYS, readClassicShapeKey, readCounterboreR, readDrillR, TAP_LAYOUT_KEYS } from './read-op-classic'
import { COUNTERBORE_KEYS, readNewShapeKey } from './read-op-shapes'
import { readPathShapeKey } from './read-op-path'
import { QprogError } from './qprog-error'

export { DRILL_KEYS } from './read-op-classic'

/** The kind word of an `op` line: the kind of the union and, for a drill, its layout word. */
export interface OpHead {
  kind: Operation['kind']
  drill: string | null
}

/** The kind of an `op` line; a drill carries its layout, so the three layouts share one arm. */
export function opHead(text: string, line: number): OpHead {
  if (text === 'drill' || text === 'drill-grid' || text === 'drill-circle') return { kind: 'drill', drill: text }
  if (text === 'face') return { kind: 'face', drill: null }
  if (text === 'pocket') return { kind: 'pocket', drill: null }
  if (text === 'circle') return { kind: 'circle-pocket', drill: null }
  if (text === 'contour') return { kind: 'contour', drill: null }
  if (text === 'slot') return { kind: 'slot', drill: null }
  if (text === 'chamfer') return { kind: 'chamfer', drill: null }
  if (text === 'tap') return { kind: 'tap', drill: null }
  if (text === 'counterbore') return { kind: 'counterbore', drill: null }
  if (text === 'thread') return { kind: 'thread', drill: null }
  if (text === 'engrave') return { kind: 'engrave', drill: null }
  if (text === 'pocket-island') return { kind: 'pocket-island', drill: null }
  if (text === 'rest') return { kind: 'rest', drill: null }
  if (text === 'custom') return { kind: 'custom', drill: null }
  if (text === 'probe') return { kind: 'probe', drill: null }
  throw new QprogError(line, `неизвестный вид операции '${text}'.`)
}

/** The shape keys of a kind without the optional `h`, `material`, `drill` and `r`. */
export const SHAPE_KEYS: Record<Operation['kind'], string[]> = {
  face: ['z', 'stepOver'],
  pocket: ['rect', 'cornerRadius', 'finishAllowance', 'stepOver'],
  'circle-pocket': ['circle', 'finishAllowance', 'stepOver'],
  drill: [],
  contour: ['points', 'closed', 'allowance', 'side'],
  slot: ['from', 'to', 'width', 'stepOver'],
  chamfer: ['from', 'to', 'chamfer'],
  tap: ['pattern', 'pitch'],
  counterbore: ['pattern', 'seat', 'seatDepth', 'dwell'],
  thread: ['hole', 'pitch', 'internal', 'threadDepth', 'allowance', 'stepOver'],
  engrave: ['text', 'x', 'y', 'height', 'angle'],
  'pocket-island': ['rect', 'island', 'islandAllowance', 'stepOver'],
  rest: ['source', 'rect', 'allowance', 'stepOver'],
  custom: [],
  probe: ['points', 'range', 'touch', 'axis'],
}

/** The common keys every kind writes. */
function commonValue(op: Operation, sub: string, line: string, words: string[], key: string, where: string): boolean {
  if (sub === 'gcode') {
    // A custom block has one line per `op.gcode`: the reader keeps the order and the blanks. A bare
    // key is an empty line, an unquoted text is taken as is (a hand edit of a comment).
    if (op.kind !== 'custom') return false
    op.gcode = [...op.gcode, words.length > 1 ? unquoteText(restAfter(line, 1)) : '']
    return true
  }
  if (sub === 'notes') {
    op.notes = words.length > 1 ? unquoteText(restAfter(line, 1)) : ''
    return true
  }
  if (sub === 'enabled') {
    arity(words, 1, key, where)
    op.enabled = onOff(words, key, where)
  } else if (sub === 'tool') {
    arity(words, 2, key, where)
    const number = words[1] === undefined ? null : parseInteger(words[1])
    if (number === null) throw new QprogError(0, `${where}op.tool ждёт номер и диаметр.`)
    const diameterMm = num(words, 2, key, where)
    // Only the number and the diameter: the H row follows the tool when the file does not set it.
    op.tool = { number, diameterMm, hNumber: 0 }
  } else if (sub === 'h') {
    arity(words, 1, key, where)
    const value = words[1] === undefined ? null : parseInteger(words[1])
    if (value === null) throw new QprogError(0, `${where}op.h ждёт целый номер коррекции.`)
    op.tool = { ...op.tool, hNumber: value }
  } else if (sub === 'material') {
    arity(words, 1, key, where)
    const value = words[1]
    if (value !== 'aluminium' && value !== 'steel')
      throw new QprogError(0, `${where}op.material ждёт aluminium или steel.`)
    op.material = value
  } else if (sub === 'spindle') {
    arity(words, 1, key, where)
    op.cutting = { ...op.cutting, spindleRpm: num(words, 1, key, where) }
  } else if (sub === 'feed' || sub === 'plunge') {
    arity(words, 1, key, where)
    const value = num(words, 1, key, where)
    // A drill has one feed: the plunge follows the working feed whatever the file says.
    const oneFeed = op.kind === 'drill' || op.kind === 'tap' || op.kind === 'counterbore' || op.kind === 'probe'
    const plunge = oneFeed ? op.cutting.feedMmMin : op.cutting.plungeFeedMmMin
    op.cutting =
      sub === 'feed'
        ? { ...op.cutting, feedMmMin: value, plungeFeedMmMin: oneFeed ? value : plunge }
        : { ...op.cutting, plungeFeedMmMin: oneFeed ? op.cutting.feedMmMin : value }
  } else if (sub === 'coolant') {
    arity(words, 1, key, where)
    const value = words[1]
    if (value !== 'off' && value !== 'mist' && value !== 'flood')
      throw new QprogError(0, `${where}op.coolant ждёт off, mist или flood.`)
    op.coolant = value
  } else if (sub === 'topZ') {
    arity(words, 1, key, where)
    op.topZ = num(words, 1, key, where)
  } else if (sub === 'depth') {
    arity(words, 1, key, where)
    op.depthMm = num(words, 1, key, where)
  } else if (sub === 'stepDown') {
    arity(words, 1, key, where)
    op.stepDownMm = num(words, 1, key, where)
  } else if (sub === 'drill') {
    arity(words, 1, key, where)
    const value = words[1]
    if (value !== 'hss' && value !== 'carbide') throw new QprogError(0, `${where}op.drill ждёт hss или carbide.`)
    ;(op as Extract<Operation, { kind: 'drill' }>).body = value
  } else {
    return false
  }
  return true
}

/** The keys of a kind: common, shape, and the optional `h`, `material`, `drill`, `r`. */
const keyKnown = (head: OpHead, sub: string): boolean =>
  COMMON_KEYS.includes(sub) ||
  // A custom block repeats its `op.gcode` by design: one line per program line, in order.
  (head.kind === 'custom' && sub === 'gcode') ||
  (head.kind === 'drill'
    ? DRILL_KEYS[head.drill as string]
    : head.kind === 'tap' || head.kind === 'counterbore'
      ? [...SHAPE_KEYS[head.kind], ...TAP_LAYOUT_KEYS]
      : SHAPE_KEYS[head.kind]
  ).includes(sub) ||
  ['h', 'material'].includes(sub) ||
  (head.kind === 'drill' && ['drill', 'r'].includes(sub)) ||
  (head.kind === 'counterbore' && ['r', ...COUNTERBORE_KEYS].includes(sub))

/**
 * Reads the value of `op.<sub>` into `op`. `where` already carries `строка N: `; a failure is a
 * `QprogError` with line 0, and `readProgram` fills the line.
 */
export function readOpValue(
  op: Operation,
  head: OpHead,
  key: string,
  line: string,
  words: string[],
  where: string,
): void {
  const sub = key.slice(3)
  if (!keyKnown(head, sub)) throw new QprogError(0, `${where}неизвестное поле '${key}' для операции этого вида.`)
  if (commonValue(op, sub, line, words, key, where)) return
  if (head.kind === 'drill') {
    if (sub === 'r') readDrillR(op, words, key, where)
    else readClassicShapeKey(op, sub, words, key, where)
    return
  }
  if (head.kind === 'counterbore' && sub === 'r') {
    readCounterboreR(op, words, key, where)
    return
  }
  if (readPathShapeKey(op, sub, words, key, where, line)) return
  if (!readNewShapeKey(op, sub, words, key, where)) readClassicShapeKey(op, sub, words, key, where)
}

export type { QuickProgram }
