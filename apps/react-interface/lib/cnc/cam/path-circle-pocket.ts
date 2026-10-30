// The round pocket by a spiral (PLAN-QUICKPROG, 7.4 and 10), the port of `path_circle_pocket.cpp`.
//
// Centre line: the pocket circle shrunk by `inset = D / 2 + finishAllowance`. The turns spread
// evenly over its radius, `turns = ceil(radius / nominal)` with the nominal step of the rectangular
// pocket, so the real radial step never exceeds `D * stepOverPct / 100` and the outermost turn keeps
// the tool body exactly the allowance away from the wall.
//
// The spiral leaves the centre along +X: the first half arc climbs to half the first step, every
// next half arc carries the radius to the next turn, and the outer turn closes with two half arcs
// on the axis. Every arc is a half turn with an end point, never a G2/G3 without axes: the viewer
// drops such an arc. Each pass plunges in the centre, walks the spiral, returns to the centre at
// the pass Z and cuts back up to the material top; the last pass is exactly the bottom.
import type { CirclePocketOperation, Operation, QuickProgram } from './ops'
import { PathBuilder } from './path-builder'
import { axialPasses } from './path-face'

const EPS = 1e-6
const PI = Math.PI

/** One half arc of the spiral: a piece of a circle around the pocket centre. */
export interface SpiralArc {
  radiusMm: number
  startAngle: number
  sweep: number
}

/** The circle-pocket layout: the centre line radius, the turns and the spiral of one pass. */
export interface CircleLayout {
  /** False when the centre line is a point or behind the wall: the tool does not fit. */
  fits: boolean
  cx: number
  cy: number
  insetMm: number
  /** Centre line radius, mm. */
  radiusMm: number
  turns: number
  /** Radial step between two neighbouring turns, mm. */
  stepMm: number
  passes: number[]
  spiral: SpiralArc[]
  /** Arc length of one pass, mm. */
  spiralMm: number
}

const circlePocket = (op: Operation): CirclePocketOperation => {
  if (op.kind !== 'circle-pocket') throw new Error(`planCirclePocket called with a ${op.kind} operation`)
  return op
}

/** The spiral layout of a round pocket. */
export function circleLayout(operation: CirclePocketOperation): CircleLayout {
  const pocket = operation.circle
  const insetMm = operation.tool.diameterMm / 2 + operation.finishAllowanceMm
  const radiusMm = pocket.r - insetMm
  const empty: CircleLayout = {
    fits: false,
    cx: pocket.x,
    cy: pocket.y,
    insetMm,
    radiusMm,
    turns: 0,
    stepMm: 0,
    passes: [],
    spiral: [],
    spiralMm: 0,
  }
  if (radiusMm <= EPS) return empty
  const nominal = Math.min(operation.tool.diameterMm * (operation.stepOverPct / 100), radiusMm)
  const turns = Math.ceil(radiusMm / nominal - EPS)
  const stepMm = radiusMm / turns
  const spiral: SpiralArc[] = [{ radiusMm: stepMm / 2, startAngle: 0, sweep: PI }]
  for (let i = 1; i < turns; i += 1) {
    const inner = stepMm * i
    const outer = stepMm * (i + 1)
    spiral.push({ radiusMm: (inner + outer) / 2, startAngle: 0, sweep: PI })
  }
  spiral.push({ radiusMm, startAngle: 0, sweep: PI })
  spiral.push({ radiusMm, startAngle: PI, sweep: PI })
  const spiralMm = spiral.reduce((sum, arc) => sum + arc.radiusMm * arc.sweep, 0)
  return {
    ...empty,
    fits: true,
    turns,
    stepMm,
    passes: axialPasses(operation.topZ, operation.depthMm, operation.stepDownMm),
    spiral,
    spiralMm,
  }
}

/** A cosine or sine of a multiple of pi that belongs on an axis is put on it exactly. */
const onAxis = (value: number): number => (Math.abs(value) < 1e-12 ? 0 : value)

/** A point of the circle around the pocket centre at `angle` radians. */
const pointAt = (layout: CircleLayout, radiusMm: number, angle: number, z: number) => ({
  x: layout.cx + radiusMm * onAxis(Math.cos(angle)),
  y: layout.cy + radiusMm * onAxis(Math.sin(angle)),
  z,
})

/** The spiral of one round pocket, appended to `out`; a tool that does not fit plans nothing. */
export function planCirclePocket(program: QuickProgram, operation: Operation, out: PathBuilder): CircleLayout {
  const op = circlePocket(operation)
  const layout = circleLayout(op)
  if (!layout.fits) return layout
  const centre = { x: layout.cx, y: layout.cy }
  const top = op.topZ
  for (const planeZ of layout.passes) {
    out.add('rapid', { ...centre, z: program.safeZ })
    out.add('rapid', { ...centre, z: top })
    out.add('cut', { ...centre, z: planeZ }, op.cutting.plungeFeedMmMin)
    for (const arc of layout.spiral) {
      const start = pointAt(layout, arc.radiusMm, arc.startAngle, planeZ)
      const to = pointAt(layout, arc.radiusMm, arc.startAngle + arc.sweep, planeZ)
      // I/J are the offset of the arc centre from the arc start, as G3 defines them.
      const move = {
        kind: 'cut' as const,
        to,
        feedMmMin: op.cutting.feedMmMin,
        arc: { cw: false, i: layout.cx - start.x, j: layout.cy - start.y },
      }
      out.addArc(move)
    }
    // Back to the centre along the axis at the pass Z, then up from the middle of the pocket.
    out.add('cut', { ...centre, z: planeZ }, op.cutting.feedMmMin)
    out.add('cut', { ...centre, z: top }, op.cutting.plungeFeedMmMin)
  }
  return layout
}
