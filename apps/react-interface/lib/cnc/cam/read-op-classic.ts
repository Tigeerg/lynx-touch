// The shape keys of the classic kinds of a `.qprog` file (PLAN-QUICKPROG, 6, 7.4), C3: drill, face,
// pocket and circle-pocket, the port of `serialize_read_op.cpp` and `qprog_operation.cpp`.
//
// The table of kinds, the common keys and the dispatcher are in `read-op.ts`; the new kinds
// (contour, slot, chamfer) in `read-op-shapes.ts`. A drill is one kind with three layouts, so its
// shape keys depend on the `op` word: `DRILL_KEYS` names them here, and the layout word is passed
// to this reader as `drill`, the second word of an `op` line. `SHAPE_KEYS` of every kind and the
// required-key check are in `read-op.ts`.
import type { Operation } from './ops'
import { parseInteger } from './serialize-words'
import { arity, num, onOff } from './read-op-words'
import { QprogError } from './qprog-error'

/** The shape keys of a drilling layout; the layout is chosen by the `op` word. */
export const DRILL_KEYS: Record<string, string[]> = {
  drill: ['hole', 'peck'],
  'drill-grid': ['grid', 'peck'],
  'drill-circle': ['circle', 'count', 'start', 'arc', 'fullTurn', 'peck'],
}

/** A drilling operation, whose common fields its layout keys write into. */
type Drill = Extract<Operation, { kind: 'drill' }>

/** The drill, whose common fields the board shares: every branch writes into it. */
const asDrill = (op: Operation): Drill => op as Drill

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
  if (sub === 'hole' || (sub === 'circle' && (op.kind === 'drill' || op.kind === 'circle-pocket'))) {
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
  } else if (sub === 'stepOver' && (op.kind === 'face' || op.kind === 'pocket' || op.kind === 'circle-pocket')) {
    // The face keeps its own step, both pockets carry theirs (PLAN-QUICKPROG, 7.3).
    arity(words, 1, key, where)
    op.stepOverPct = num(words, 1, key, where)
  } else {
    throw new QprogError(0, `${where}неизвестное поле '${key}' для операции этого вида.`)
  }
}

/** The `r` of a drill: the R plane of the cycle, mm, and the flag that the document set it. */
export function readDrillR(op: Operation, words: string[], key: string, where: string): void {
  arity(words, 1, key, where)
  const drill = asDrill(op)
  drill.rMm = num(words, 1, key, where)
  drill.hasR = true
}

/** The layout keys a tap and a counterbore share with a drill: the same words, the same fields. */
export const TAP_LAYOUT_KEYS: readonly string[] = ['hole', 'grid', 'circle', 'count', 'start', 'arc', 'fullTurn']

/** The `r` of a counterbore: the R plane of G82, required by its check, read as the drill's. */
export function readCounterboreR(op: Operation, words: string[], key: string, where: string): void {
  arity(words, 1, key, where)
  const cb = op as Extract<Operation, { kind: 'counterbore' }>
  cb.rMm = num(words, 1, key, where)
  cb.hasR = true
}

/** Reads one shape key of a classic kind into `op`; a key of another kind is a failure. */
export function readClassicShapeKey(op: Operation, sub: string, words: string[], key: string, where: string): void {
  // A tap and a counterbore reuse the drilling layout keys: the same `hole`/`grid`/`circle` words.
  if (op.kind === 'drill' || ((op.kind === 'tap' || op.kind === 'counterbore') && TAP_LAYOUT_KEYS.includes(sub))) {
    drillShape(op, sub, words, key, where)
  } else millShape(op, sub, words, key, where)
}
