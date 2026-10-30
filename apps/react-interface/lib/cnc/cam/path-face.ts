// The face milling planner (PLAN-QUICKPROG, 7.4 and 10), the port of `path_face.cpp`.
//
// Strips run along the long side of the stock and step across the short one at `stepOverPct` % of
// the diameter. The strip centres span `usable = across - D`: the first sits half a diameter inside
// one edge, the last half a diameter inside the other, so the tool body reaches both edges with no
// uncut rim. The count is `ceil(usable / nominal) + 1`, so the real step `usable / (strips - 1)`
// never exceeds the nominal one and the last strip lands exactly on the far edge.
//
// The strips sweep `lead = max(10 % D, 1 mm)` past both ends, without an upper cap: a tool of
// radius R leaves a half disc at the end of an open strip, so a lead "to size" has to reach at
// least R, and a small tool gets the 1 mm minimum. The across direction adds no overrun: the face
// covers exactly the width.
//
// Every strip is an open slot: the tool rapids at safe Z over the strip start, rapids down to the
// material top (never below it), plunges to the pass Z, cuts the strip, ramps back up to the top,
// then the next strip starts from there. Each Z pass repeats the strips one step deeper; the last
// pass is exactly the bottom.
import type { FaceOperation, Operation, QuickProgram, StockBox } from './ops'
import { EPS, PathBuilder } from './path-builder'

/** The share of the diameter a strip leads past the stock, per side. */
const OVERRUN_SHARE = 0.1
/** The shortest face lead, mm. */
const MIN_LEAD_MM = 1

/** The face layout: strips across the stock and the axial passes from the top down. */
export interface FaceLayout {
  /** True when the strips sweep X (the stock is at least as long in X). */
  alongX: boolean
  /** Across step between neighbouring strips, mm. */
  stripStepMm: number
  strips: number
  /** Across coordinate of the first strip, mm. */
  stripStart: number
  /** The lead along the strip past each stock end, mm. */
  edgeOverrunMm: number
  /** The Z of each pass, top down; the last is exactly the bottom. */
  passes: number[]
}

const face = (op: Operation): FaceOperation => {
  if (op.kind !== 'face') throw new Error(`planFace called with a ${op.kind} operation`)
  return op
}

/** The face lead: `share` of the diameter, never shorter than `minMm`, never capped at the radius. */
export const faceLead = (diameterMm: number, share = OVERRUN_SHARE, minMm = MIN_LEAD_MM): number =>
  Math.max(diameterMm * share, minMm)

/** The axial passes from the top down: the step, then the bottom exactly, never a tail below it. */
export const axialPasses = (topZ: number, depthMm: number, stepDownMm: number): number[] => {
  const bottom = topZ - depthMm
  const passes: number[] = []
  let z = topZ
  if (stepDownMm > 0) {
    while (z - stepDownMm > bottom + EPS) {
      z -= stepDownMm
      passes.push(z)
    }
  }
  passes.push(bottom)
  return passes
}

/** The layout of the face: strips, the real step and the passes. */
export function faceLayout(stock: StockBox, operation: FaceOperation): FaceLayout {
  const d = operation.tool.diameterMm
  const half = d / 2
  const sizeX = stock.max.x - stock.min.x
  const sizeY = stock.max.y - stock.min.y
  const alongX = sizeX >= sizeY
  const across = alongX ? sizeY : sizeX
  const acrossMin = alongX ? stock.min.y : stock.min.x
  const edgeOverrunMm = faceLead(d)
  const usable = across - d
  const nominal = Math.min(Math.max(d * (operation.stepOverPct / 100), EPS), d)
  const passes = axialPasses(operation.topZ, operation.depthMm, operation.stepDownMm)
  if (usable <= EPS) {
    // The tool is at least as wide as the stock: one strip through the centre covers the width.
    return { alongX, stripStepMm: 0, strips: 1, stripStart: acrossMin + across / 2, edgeOverrunMm, passes }
  }
  const strips = Math.ceil(usable / nominal - EPS) + 1
  return {
    alongX,
    stripStepMm: usable / (strips - 1),
    strips,
    stripStart: acrossMin + half,
    edgeOverrunMm,
    passes,
  }
}

/** The face strips of one operation, appended to `out`. A valid document is assumed. */
export function planFace(program: QuickProgram, operation: Operation, out: PathBuilder): FaceLayout {
  const op = face(operation)
  const layout = faceLayout(program.stock, op)
  const alongMin = layout.alongX ? program.stock.min.x : program.stock.min.y
  const alongMax = layout.alongX ? program.stock.max.x : program.stock.max.y
  const start = alongMin - layout.edgeOverrunMm
  const end = alongMax + layout.edgeOverrunMm
  const safeZ = program.safeZ
  const top = op.topZ
  const point = (along: number, across: number, z: number) =>
    layout.alongX ? { x: along, y: across, z } : { x: across, y: along, z }
  let lastAcross = layout.stripStart
  for (const planeZ of layout.passes) {
    for (let i = 0; i < layout.strips; i += 1) {
      lastAcross = layout.stripStart + i * layout.stripStepMm
      out.add('rapid', point(start, lastAcross, safeZ))
      // The descent is its own rapid: the demo's console counts it, and the file writes `Z<top>`
      // on the next line anyway (`emit_moves.cpp`).
      out.add('rapid', point(start, lastAcross, top))
      out.add('cut', point(start, lastAcross, planeZ), op.cutting.plungeFeedMmMin)
      out.add('cut', point(end, lastAcross, planeZ), op.cutting.feedMmMin)
      out.add('cut', point(end, lastAcross, top), op.cutting.plungeFeedMmMin)
    }
    out.add('rapid', point(end, lastAcross, safeZ))
  }
  return layout
}
