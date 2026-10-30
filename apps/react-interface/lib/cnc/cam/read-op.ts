// One operation of a `.qprog` file (PLAN-QUICKPROG, 6, 7.4), C3, the port of
// `serialize_read_op.cpp` and `qprog_operation.cpp`.
//
// This module owns the table of kinds and the arity of one `op.*` key: a new key is one branch here
// and one line in `serialize-write.ts`. The line loop is in `read-program.ts`. A contour is the one
// kind whose shape is a list: `op.points` takes pairs `x y` to the end of the line, the rest of the
// keys keep their fixed arity.
import type { Operation, QuickProgram } from './ops'
import { COMMON_KEYS } from './read-header'
import { parseInteger, parseNumber, parseOnOff, restAfter, unquoteText } from './serialize-words'
import { QprogError } from './qprog-error'

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
  throw new QprogError(line, `неизвестный вид операции '${text}'.`)
}

/** The shape keys of a kind without the optional `h`, `material`, `drill` and `r`. */
export const SHAPE_KEYS: Record<Operation['kind'], string[]> = {
  face: ['z', 'stepOver'],
  pocket: ['rect', 'cornerRadius', 'finishAllowance', 'stepOver'],
  'circle-pocket': ['circle', 'finishAllowance', 'stepOver'],
  drill: [],
  contour: ['points', 'closed', 'allowance', 'side'],
}

/** The shape keys of a drilling layout; the layout is chosen by the `op` word. */
export const DRILL_KEYS: Record<string, string[]> = {
  drill: ['hole', 'peck'],
  'drill-grid': ['grid', 'peck'],
  'drill-circle': ['circle', 'count', 'start', 'arc', 'fullTurn', 'peck'],
}

/** One numeric value: the field of `words[index]`, or a failure that names the key. */
const num = (words: string[], index: number, key: string, where: string): number => {
  const value = words[index] === undefined ? null : parseNumber(words[index])
  if (value === null) throw new QprogError(0, `${where}${key} ждёт число.`)
  return value
}

/** The exact arity of a key: `count` values, not more and not fewer. */
const arity = (words: string[], count: number, key: string, where: string): void => {
  if (words.length !== count + 1) {
    throw new QprogError(
      0,
      `${where}поле '${key}' ждёт ${String(count)} знач., а получило ${String(words.length - 1)}.`,
    )
  }
}

/** The `on`/`off` value of `words[1]`, or a failure that names the key. */
const onOff = (words: string[], key: string, where: string): boolean => {
  const value = words.length === 2 ? parseOnOff(words[1]) : null
  if (value === null) throw new QprogError(0, `${where}${key} ждёт 'on' или 'off'.`)
  return value
}

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

/** The drill, whose common fields the board shares: every branch writes into it. */
const asDrill = (op: Operation): Extract<Operation, { kind: 'drill' }> => op as Extract<Operation, { kind: 'drill' }>

/** The `circle` key of a circle pocket and of a ring: the same three numbers, another field. */
const writeCircle = (op: Operation, sub: string, words: string[], key: string, where: string): void => {
  arity(words, 3, key, where)
  const x = num(words, 1, key, where)
  const y = num(words, 2, key, where)
  const r = num(words, 3, key, where)
  if (sub === 'hole') {
    asDrill(op).hole = { x, y, r }
  } else if (op.kind === 'circle-pocket') {
    op.circle = { x, y, r }
  } else {
    asDrill(op).hole = { x, y, r }
  }
}

/** The geometry keys of a drilling layout: `hole`, `grid`, `circle`, `count`, `start`, `arc`. */
function drillShape(op: Operation, sub: string, words: string[], key: string, where: string): void {
  const drill = asDrill(op)
  if (sub === 'hole' || sub === 'circle') {
    writeCircle(op, sub, words, key, where)
  } else if (sub === 'grid') {
    arity(words, 6, key, where)
    const cols = words[3] === undefined ? null : parseInteger(words[3])
    const rows = words[4] === undefined ? null : parseInteger(words[4])
    if (cols === null || rows === null) throw new QprogError(0, `${where}op.grid ждёт целые cols и rows.`)
    drill.x0 = num(words, 1, key, where)
    drill.y0 = num(words, 2, key, where)
    drill.cols = cols
    drill.rows = rows
    drill.dx = num(words, 5, key, where)
    drill.dy = num(words, 6, key, where)
  } else if (sub === 'count') {
    arity(words, 1, key, where)
    const value = words[1] === undefined ? null : parseInteger(words[1])
    if (value === null) throw new QprogError(0, `${where}op.count ждёт целое.`)
    drill.count = value
  } else if (sub === 'start' || sub === 'arc') {
    arity(words, 1, key, where)
    const value = num(words, 1, key, where)
    if (sub === 'start') drill.startDeg = value
    else drill.arcDeg = value
  } else if (sub === 'fullTurn') {
    arity(words, 1, key, where)
    drill.fullTurn = onOff(words, key, where)
  } else if (sub === 'peck' || sub === 'r') {
    arity(words, 1, key, where)
    const value = num(words, 1, key, where)
    if (sub === 'peck') drill.peckMm = value
    else {
      drill.rMm = value
      drill.hasR = true
    }
  }
}

/** The geometry keys of a milling kind: `z`, `rect`, `circle`, `cornerRadius`, the allowance. */
function millShape(op: Operation, sub: string, words: string[], key: string, where: string): void {
  if (sub === 'circle') {
    writeCircle(op, sub, words, key, where)
  } else if (sub === 'z' && op.kind === 'face') {
    arity(words, 1, key, where)
    op.z = num(words, 1, key, where)
  } else if (sub === 'rect' && op.kind === 'pocket') {
    arity(words, 4, key, where)
    const x = num(words, 1, key, where)
    const y = num(words, 2, key, where)
    const w = num(words, 3, key, where)
    const h = num(words, 4, key, where)
    op.rect = { x, y, w, h }
  } else if (sub === 'cornerRadius' && op.kind === 'pocket') {
    arity(words, 1, key, where)
    op.cornerRadiusMm = num(words, 1, key, where)
  } else if (sub === 'finishAllowance' && (op.kind === 'pocket' || op.kind === 'circle-pocket')) {
    arity(words, 1, key, where)
    op.finishAllowanceMm = num(words, 1, key, where)
  } else if (sub === 'points' && op.kind === 'contour') {
    op.points = contourPoints(words.slice(1), where)
  } else if (sub === 'closed' && op.kind === 'contour') {
    arity(words, 1, key, where)
    op.closed = onOff(words, key, where)
  } else if (sub === 'allowance' && op.kind === 'contour') {
    arity(words, 1, key, where)
    op.allowanceMm = num(words, 1, key, where)
  } else if (sub === 'side' && op.kind === 'contour') {
    arity(words, 1, key, where)
    if (words[1] !== 'material' && words[1] !== 'air') throw new QprogError(0, `${where}op.side ждёт material или air.`)
    op.side = words[1]
  } else if (sub === 'stepOver' && (op.kind === 'face' || op.kind === 'pocket' || op.kind === 'circle-pocket')) {
    // The face keeps its own step; both pockets carry theirs (PLAN-QUICKPROG, 7.3).
    arity(words, 1, key, where)
    op.stepOverPct = num(words, 1, key, where)
  } else {
    throw new QprogError(0, `${where}неизвестное поле '${key}' для операции этого вида.`)
  }
}

/** The common keys every kind writes. */
function commonValue(op: Operation, sub: string, line: string, words: string[], key: string, where: string): boolean {
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
    const plunge = op.kind === 'drill' ? op.cutting.feedMmMin : op.cutting.plungeFeedMmMin
    op.cutting =
      sub === 'feed'
        ? { ...op.cutting, feedMmMin: value, plungeFeedMmMin: op.kind === 'drill' ? value : plunge }
        : { ...op.cutting, plungeFeedMmMin: op.kind === 'drill' ? op.cutting.feedMmMin : value }
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
    asDrill(op).body = value
  } else {
    return false
  }
  return true
}

/** The keys of a kind: common, shape, and the optional `h`, `material`, `drill`, `r`. */
const keyKnown = (head: OpHead, sub: string): boolean =>
  COMMON_KEYS.includes(sub) ||
  (head.kind === 'drill' ? DRILL_KEYS[head.drill as string] : SHAPE_KEYS[head.kind]).includes(sub) ||
  ['h', 'material'].includes(sub) ||
  (head.kind === 'drill' && ['drill', 'r'].includes(sub))

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
  if (head.kind === 'drill') drillShape(op, sub, words, key, where)
  else millShape(op, sub, words, key, where)
}

export type { QuickProgram }
