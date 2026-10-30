// Thread milling (PLAN-QUICKPROG 7.6): the helix of one pitch per turn, its radius and its passes.
//
// The mill centre turns on a circle of `thread radius -/+ D/2`: an internal thread keeps the outer
// point of the mill on the crest (`r - D/2`), an external one keeps the inner point on the flank
// (`r + D/2`). One turn climbs one pitch, so the turns are `threadDepth / pitch`. The first radial
// pass stands `threadAllowanceMm` off the final circle, every next one a step closer (`stepOverPct`
// of the diameter, as of a pocket), and the last pass is exactly the thread circle: the finish.
//
// A whole turn is two half arcs with an end point (the arc rule of 7.1): the viewer drops an arc
// without axes, and an arc of pi is written with I/J only. An internal thread turns clockwise (G2,
// the climb on the right of the walk), an external one counter-clockwise (G3).
import type { Operation, QuickProgram, ThreadOperation } from './ops'
import { PathBuilder } from './path-builder'

const EPS = 1e-6
const PI = Math.PI

/** The helix of one thread: the circle radius, the turns, the climb and the radial passes. */
export interface ThreadLayout {
  /** False when the thread circle is a point or behind the mill: the mill does not fit. */
  fits: boolean
  cx: number
  cy: number
  /** The thread radius named by the operation, mm. */
  threadRadiusMm: number
  /** The circle of the mill centre, mm. */
  radiusMm: number
  /** The turns of one pass: `threadDepth / pitch`. */
  turns: number
  /** The climb of the helix, mm per turn. */
  pitchMm: number
  /** The radius of the mill centre on every radial pass; the last one is the finish. */
  passes: number[]
}

const asThread = (op: Operation): ThreadOperation => {
  if (op.kind !== 'thread') throw new Error(`planThread called with a ${op.kind} operation`)
  return op
}

/** The radius of the mill centre: inside the crest for a nut, outside the flank for a stud. */
export function threadRadius(operation: ThreadOperation): number {
  const mill = operation.tool.diameterMm / 2
  return operation.internal ? operation.hole.r - mill : operation.hole.r + mill
}

/**
 * The helix layout of one thread. The radial passes start `threadAllowanceMm` outside the final
 * circle and come in by `D * stepOverPct / 100`, the last pass exactly on the thread circle.
 */
export function threadLayout(operation: ThreadOperation): ThreadLayout {
  const radiusMm = threadRadius(operation)
  const pitch = operation.pitchMm
  const turns = pitch > EPS ? operation.threadDepthMm / pitch : 0
  const empty: ThreadLayout = {
    fits: false,
    cx: operation.hole.x,
    cy: operation.hole.y,
    threadRadiusMm: operation.hole.r,
    radiusMm,
    turns,
    pitchMm: pitch,
    passes: [],
  }
  if (radiusMm <= EPS || !(turns > 0)) return empty
  const step = Math.max(operation.tool.diameterMm * (operation.stepOverPct / 100), EPS)
  const allowance = Math.max(operation.threadAllowanceMm, 0)
  // `passes` counts every radial pass, the finish included. One pass with no allowance is a single
  // helix on the thread circle; a rough pass stands one step off it, and the finish comes last.
  const last = allowance > EPS ? Math.ceil(allowance / step) + 1 : 1
  const passes: number[] = []
  for (let i = 0; i < last - 1; i += 1) {
    passes.push(radiusMm + Math.min((i + 1) * step, allowance))
  }
  passes.push(radiusMm)
  return { ...empty, fits: true, passes }
}

/** A cosine or sine of a multiple of pi that belongs on an axis is put on it exactly. */
const onAxis = (value: number): number => (Math.abs(value) < 1e-12 ? 0 : value)

/** The point of the circle around the thread centre at `angle`, at the helix Z of `turn`. */
const pointAt = (layout: ThreadLayout, radiusMm: number, angle: number, z: number) => ({
  x: layout.cx + radiusMm * onAxis(Math.cos(angle)),
  y: layout.cy + radiusMm * onAxis(Math.sin(angle)),
  z,
})

/**
 * The cuts of one radial pass: the helix from `startZ` down, two half arcs per turn, each half arc
 * climbing `pitch / 2`. The first half arc starts at the top point of the circle (+Y): it is the
 * half turn to -Y, so a full turn always ends on a real point of the circle.
 */
function helixMoves(
  layout: ThreadLayout,
  radiusMm: number,
  startZ: number,
  cw: boolean,
  feedMmMin: number,
  out: PathBuilder,
): void {
  const halves = Math.max(Math.round(layout.turns * 2), 2)
  const climb = layout.pitchMm / 2
  const sweep = cw ? -PI : PI
  let angle = PI / 2
  for (let i = 0; i < halves; i += 1) {
    const from = pointAt(layout, radiusMm, angle, startZ - climb * i)
    const next = angle + sweep
    const to = pointAt(layout, radiusMm, next, startZ - climb * (i + 1))
    out.addArc({
      kind: 'cut',
      to,
      feedMmMin,
      // I/J are the offset of the arc centre from the arc start, as the control defines them.
      arc: { cw, i: layout.cx - from.x, j: layout.cy - from.y },
    })
    angle = next
  }
}

/** The helix of one thread, appended to `out`; a mill that does not fit plans nothing. */
export function planThread(program: QuickProgram, operation: Operation, out: PathBuilder): ThreadLayout {
  const op = asThread(operation)
  const layout = threadLayout(op)
  if (!layout.fits) return layout
  const centre = { x: layout.cx, y: layout.cy }
  const top = op.topZ
  const cw = op.internal
  for (const radius of layout.passes) {
    const start = pointAt(layout, radius, PI / 2, top)
    out.add('rapid', { ...centre, z: program.safeZ })
    out.add('rapid', start)
    out.add('cut', start, op.cutting.plungeFeedMmMin)
    helixMoves(layout, radius, top, cw, op.cutting.feedMmMin, out)
    // The mill leaves the circle along the radius at the bottom of the helix, then rises in the
    // middle of the hole, so it never drags a full thread turn on the way out.
    out.add('cut', { ...centre, z: top - layout.turns * layout.pitchMm }, op.cutting.feedMmMin)
    out.add('cut', { ...centre, z: top }, op.cutting.plungeFeedMmMin)
  }
  return layout
}
