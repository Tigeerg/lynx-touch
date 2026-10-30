// The slot planner (PLAN-QUICKPROG, 7.6): a straight keyway cut by one or more longitudinal passes.
//
// The walk is the straight from `from` to `to`; its length is the slot, its normal is the across
// direction. A slot no wider than the mill is one pass straight down its centre, from `from` to `to`.
// A wider slot is cut by strips across its width: the strips span `usable = width - D`, so the first
// strip stands half a diameter inside one wall and the last half a diameter inside the other, and
// the count is `ceil(usable / nominal) + 1` with `nominal = D * stepOverPct / 100`; the real step
// `usable / (strips - 1)` never exceeds the nominal one, so neighbouring strips overlap and no band
// of the slot stays uncut. The strips spread evenly across the width from one wall to the other.
//
// A pass: rapid at safe Z over the strip start, rapid down to the material top, plunge to the pass Z,
// the strip at the working feed, and a cut back up to the top. The strips of one Z pass alternate
// direction; each Z pass repeats the strips one step deeper and the last pass is exactly the floor.
// A width exactly the mill is one strip with `stepMm` zero.
import type { Operation, ProgramShape, SlotOperation } from './ops'
import { direction } from './contour-offset'
import type { Vec2 } from './contour-offset'
import { axialPasses } from './path-face'
import { EPS, PathBuilder } from './path-builder'

/** The slot layout: one strip across the width, the real step and the axial passes. */
export interface SlotStrip {
  /** The mill centre at the start and at the end of the strip, mm. */
  from: Vec2
  to: Vec2
  /** The strip centre across the slot, mm: the distance from the slot axis, signed. */
  acrossMm: number
}

export interface SlotLayout {
  /** The strips across the width, in the order the walk visits them. */
  strips: SlotStrip[]
  /** Real step between neighbouring strips, mm; zero for a single strip. */
  stepMm: number
  /** The Z of each pass, top down; the last is exactly the floor. */
  passes: number[]
}

const slot = (op: Operation): SlotOperation => {
  if (op.kind !== 'slot') throw new Error(`planSlot called with a ${op.kind} operation`)
  return op
}

const flat = (point: { x: number; y: number }): Vec2 => ({ x: point.x, y: point.y })

/** The strips of a slot: their ends across the width, the real step and the passes. */
export function slotLayout(operation: SlotOperation): SlotLayout {
  const op = slot(operation)
  const passes = axialPasses(op.topZ, op.depthMm, op.stepDownMm)
  const from = flat(op.from)
  const to = flat(op.to)
  const dir = direction(from, to)
  if (dir === null) return { strips: [], stepMm: 0, passes }
  const d = op.tool.diameterMm
  // The normal points across the slot: `usable` is measured along it either way, so the sign of the
  // normal only decides the order of the strips and never where the slot starts.
  const normal = { x: -dir.y, y: dir.x }
  const usable = op.widthMm - d
  const centres = stripCentres(usable, d, op.stepOverPct)
  const stepMm = centres.length > 1 ? Math.abs(centres[1] - centres[0]) : 0
  const strips: SlotStrip[] = centres.map((across) => ({
    from: { x: from.x + normal.x * across, y: from.y + normal.y * across },
    to: { x: to.x + normal.x * across, y: to.y + normal.y * across },
    acrossMm: across,
  }))
  return { strips, stepMm, passes }
}

/**
 * The across offsets of the strips from the slot centre, in the order they are cut. A slot as wide
 * as the mill gives the single centre 0; a wider one spreads `strips` centres evenly from
 * `-usable/2` to `+usable/2`: the real step `h = usable / (strips - 1)` never exceeds the nominal
 * one, so neighbouring strips overlap and no band of the slot stays uncut.
 */
function stripCentres(usable: number, diameterMm: number, stepOverPct: number): number[] {
  if (usable <= EPS) return [0]
  const nominal = Math.min(Math.max(diameterMm * (stepOverPct / 100), EPS), diameterMm)
  const strips = Math.ceil(usable / nominal - EPS) + 1
  const step = usable / (strips - 1)
  const offsets: number[] = []
  for (let i = 0; i < strips; i += 1) offsets.push(i * step - usable / 2)
  return offsets
}

/**
 * The slot of one operation, appended to `out`. A valid document is assumed. A strip is one run and
 * the walk turns at every strip, so the strips alternate direction; each strip is its own plunge and
 * retract, as a face strip is.
 */
export function planSlot(
  program: ProgramShape & { safeZ: number },
  operation: Operation,
  out: PathBuilder,
): SlotLayout {
  const op = slot(operation)
  const layout = slotLayout(op)
  if (layout.strips.length === 0) return layout
  const safeZ = program.safeZ
  const top = op.topZ
  const point = (item: { x: number; y: number }, z: number) => ({ x: item.x, y: item.y, z })
  for (const planeZ of layout.passes) {
    layout.strips.forEach((strip, index) => {
      const forward = index % 2 === 0
      const start = forward ? strip.from : strip.to
      const end = forward ? strip.to : strip.from
      out.add('rapid', point(start, safeZ))
      // The descent is its own rapid, as a face strip does: the console counts the same moves.
      out.add('rapid', point(start, top))
      out.add('cut', point(start, planeZ), op.cutting.plungeFeedMmMin)
      out.add('cut', point(end, planeZ), op.cutting.feedMmMin)
      out.add('cut', point(end, top), op.cutting.plungeFeedMmMin)
    })
  }
  return layout
}
