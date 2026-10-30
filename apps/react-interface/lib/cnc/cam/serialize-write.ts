// The `.qprog` writer (PLAN-QUICKPROG, 6, 7.4), C3, the port of `serialize.cpp`.
//
// The format is the demo's, byte for byte: the header keys first, then one `op <kind> "<id>"` and its
// `op.*` lines per operation. The writer is deterministic: the same document gives the same text,
// keys in a fixed order, numbers in one spelling. Fields the console treats as optional are left out
// when they hold the default, so a panel file stays as clean as a demo file:
//
//   * `op.h` when the H row is not the tool number (zero is the same "the tool number");
//   * `op.material` for aluminium and `op.drill` for HSS (the rows of the older file);
//   * `op.notes` when empty, and `op.r` when the R plane was not set.
//
// `g64P`/`g64Q` of a non-positive tolerance are written as a bare key: that is the console's "use
// the default" and `validate` refuses the value on a run, so the file says what was typed.
import type { DrillOperation, Material, Operation, QuickProgram, TapOperation } from './ops'
import { qprogNumber as num, KIND_WORDS, onOff, quoteText } from './serialize-words'

/** The drill word of a kind on the `op` line: the ring is `drill-circle`, the default. */
const drillWord = (op: DrillOperation): string =>
  op.pattern === 'grid' ? 'drill-grid' : op.pattern === 'circle' ? 'drill-circle' : 'drill'

/** The layout lines of a tap: one `op.pattern` word, then the same keys a drill layout writes. */
function tapLines(op: TapOperation): string {
  const out = `op.pattern ${op.pattern}\nop.pitch ${num(op.pitchMm)}\n`
  switch (op.pattern) {
    case 'hole':
      return out + `op.hole ${num(op.hole.x)} ${num(op.hole.y)} ${num(op.hole.r)}\n`
    case 'grid':
      return (
        out +
        `op.grid ${num(op.x0)} ${num(op.y0)} ${String(op.cols)} ${String(op.rows)} ` +
        `${num(op.dx)} ${num(op.dy)}\n`
      )
    case 'circle':
      return (
        out +
        `op.circle ${num(op.hole.x)} ${num(op.hole.y)} ${num(op.hole.r)}\n` +
        `op.count ${String(op.count)}\nop.start ${num(op.startDeg)}\nop.arc ${num(op.arcDeg)}\n` +
        `op.fullTurn ${onOff(op.fullTurn)}\n`
      )
  }
}

/**
 * The lines of a custom block: one `op.gcode <text>` per program line, the text quoted exactly as
 * `op.notes` and `op.name` are. An empty line is the bare key, so the list keeps its blanks.
 */
const gcodeLines = (lines: string[]): string =>
  lines.map((line) => (line.length === 0 ? 'op.gcode\n' : `op.gcode ${quoteText(line)}\n`)).join('')

/** The stock material line: aluminium is the default of an older file, so only steel is written. */
const materialLine = (material: Material): string => (material === 'aluminium' ? '' : 'op.material steel\n')

/** The drill body line: HSS is the default of an older file, so only carbide is written. */
const bodyLine = (op: DrillOperation): string => (op.body === 'hss' ? '' : 'op.drill carbide\n')

/** The `op.*` lines of a drilling operation, shared by the three layouts. */
const drillCommon = (op: DrillOperation): string => {
  let out = `op.peck ${num(op.peckMm)}\n`
  if (op.hasR) out += `op.r ${num(op.rMm)}\n`
  return out + bodyLine(op)
}

/** The `op.*` lines of a drilling operation, in the demo order. */
function drillLines(op: DrillOperation): string {
  switch (op.pattern) {
    case 'hole':
      return `op.hole ${num(op.hole.x)} ${num(op.hole.y)} ${num(op.hole.r)}\n${drillCommon(op)}`
    case 'grid':
      return (
        `op.grid ${num(op.x0)} ${num(op.y0)} ${String(op.cols)} ${String(op.rows)} ` +
        `${num(op.dx)} ${num(op.dy)}\n${drillCommon(op)}`
      )
    case 'circle':
      return (
        `op.circle ${num(op.hole.x)} ${num(op.hole.y)} ${num(op.hole.r)}\n` +
        `op.count ${String(op.count)}\nop.start ${num(op.startDeg)}\nop.arc ${num(op.arcDeg)}\n` +
        `op.fullTurn ${onOff(op.fullTurn)}\n${drillCommon(op)}`
      )
  }
}

/** The geometry lines of one operation, after the common ones. */
function shapeLines(op: Operation): string {
  switch (op.kind) {
    case 'face':
      return `op.z ${num(op.z)}\nop.stepOver ${num(op.stepOverPct)}\n`
    case 'pocket':
      return (
        `op.rect ${num(op.rect.x)} ${num(op.rect.y)} ${num(op.rect.w)} ${num(op.rect.h)}\n` +
        `op.cornerRadius ${num(op.cornerRadiusMm)}\nop.finishAllowance ${num(op.finishAllowanceMm)}\n` +
        `op.stepOver ${num(op.stepOverPct)}\n`
      )
    case 'circle-pocket':
      return (
        `op.circle ${num(op.circle.x)} ${num(op.circle.y)} ${num(op.circle.r)}\n` +
        `op.finishAllowance ${num(op.finishAllowanceMm)}\nop.stepOver ${num(op.stepOverPct)}\n`
      )
    case 'contour':
      // One line of pairs, the same one-line-per-list style as `op.rect`; the reader takes the rest
      // of the line, so the list has no fixed arity and a point is `x y`.
      return (
        `op.points ${op.points.map((point) => `${num(point.x)} ${num(point.y)}`).join(' ')}\n` +
        `op.closed ${onOff(op.closed)}\nop.allowance ${num(op.allowanceMm)}\nop.side ${op.side}\n`
      )
    case 'slot':
      // The ends carry their `z`, the plane of the cut is the operation's top.
      return (
        `op.from ${num(op.from.x)} ${num(op.from.y)} ${num(op.from.z)}\n` +
        `op.to ${num(op.to.x)} ${num(op.to.y)} ${num(op.to.z)}\n` +
        `op.width ${num(op.widthMm)}\nop.stepOver ${num(op.stepOverPct)}\n`
      )
    case 'chamfer':
      return (
        `op.from ${num(op.from.x)} ${num(op.from.y)} ${num(op.from.z)}\n` +
        `op.to ${num(op.to.x)} ${num(op.to.y)} ${num(op.to.z)}\n` +
        `op.chamfer ${num(op.chamferMm)}\n`
      )
    case 'drill':
      return drillLines(op)
    case 'tap':
      return tapLines(op)
    case 'custom':
      return gcodeLines(op.gcode)
    case 'probe':
      // The points carry all three coordinates: a touch along X or Y names the surface side too.
      return (
        `op.points ${op.points.map((point) => `${num(point.x)} ${num(point.y)} ${num(point.z)}`).join(' ')}\n` +
        `op.range ${num(op.rangeMm)}\nop.touch ${num(op.touchFeedMmMin)}\nop.axis ${op.axis}\n`
      )
  }
}

/** The common `op.*` lines of an operation, in the demo order (material and H after the tool). */
function commonLines(op: Operation): string {
  const id = op.id.length > 0 ? ` ${quoteText(op.id)}` : ''
  const kind = op.kind === 'drill' ? drillWord(op) : KIND_WORDS[op.kind]
  let out = `op ${kind}${id}\nop.enabled ${onOff(op.enabled)}\n`
  out += `op.tool ${String(op.tool.number)} ${num(op.tool.diameterMm)}\n`
  out += materialLine(op.material)
  // A drill, a tap and a probe have one feed: their plunge line repeats the working feed, so a hand
  // edit of one number cannot leave a stale second one behind. The probe's own touch feed travels
  // in `op.touch`, and the working feed is the one the form compares it against.
  const oneFeed = op.kind === 'drill' || op.kind === 'tap' || op.kind === 'probe'
  const plunge = oneFeed ? op.cutting.feedMmMin : op.cutting.plungeFeedMmMin
  // `op.h` is written only when the H row differs from the tool number: the demo writer leaves it
  // out for the common case (row = tool, or zero meaning the same), so its files round-trip.
  if (op.tool.hNumber !== 0 && op.tool.hNumber !== op.tool.number) out += `op.h ${String(op.tool.hNumber)}\n`
  out += `op.spindle ${num(op.cutting.spindleRpm)}\nop.feed ${num(op.cutting.feedMmMin)}\n`
  out += `op.plunge ${num(plunge)}\nop.coolant ${op.coolant}\n`
  out += `op.topZ ${num(op.topZ)}\nop.depth ${num(op.depthMm)}\nop.stepDown ${num(op.stepDownMm)}\n`
  if (op.notes.length > 0) out += `op.notes ${quoteText(op.notes)}\n`
  return out
}

/** The whole document as text; every line ends with a line feed, as the console files do. */
export function writeProgram(program: QuickProgram): string {
  const stock = program.stock
  let out = `schema ${String(program.schema)}\n`
  out += `name ${quoteText(program.name)}\n`
  out += `units ${program.units}\nwcs ${program.wcs}\n`
  out += `stock ${num(stock.min.x)} ${num(stock.min.y)} ${num(stock.min.z)} `
  out += `${num(stock.max.x)} ${num(stock.max.y)} ${num(stock.max.z)}\n`
  out += `safeZ ${num(program.safeZ)}\n`
  // A non-positive tolerance is the bare key: the document keeps "use the default", and `validate`
  // refuses the value on a run, so the file says what was typed.
  out += program.g64P > 0 ? `g64P ${num(program.g64P)}\n` : 'g64P\n'
  out += program.g64Q > 0 ? `g64Q ${num(program.g64Q)}\n` : 'g64Q\n'
  out += `stopAfterOp ${onOff(program.stopAfterOp)}\n`
  for (const op of program.operations) out += commonLines(op) + shapeLines(op)
  return out
}
