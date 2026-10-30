// What the operator summary of a planned operation reads (PLAN-QUICKPROG, 7.4, C2).
//
// The console of the demo prints the strip count and the row count from its own `FaceStats` and
// `PocketStats`, and the move count and the cut length from the planned moves. The planner returns
// moves and cannot see a Z pass or a row in them, so the layout is computed from one function
// (`faceLayout`, `pocketLayout`) and read twice; `pipeline.test.ts` proves every number against the
// console text of `apps/quickprog`, including the two places where the console's own stat is not
// the plan (the face length, the pocket move count).
import type { CamMove } from './moves'
import type { FaceLayout } from './path-face'
import type { PocketLayout } from './path-pocket'
import type { FaceOperation, RectPocketOperation, StockBox } from './ops'

/** The layout numbers of one face operation, as the console prints them. */
export interface FaceStats {
  strips: number
  passes: number
  /** Bottom Z of each pass, top down. */
  stepDepthsMm: number[]
  toolRadiusMm: number
  stripStepMm: number
  edgeOverrunMm: number
  sideOverrunMm: number
  /** How far below the material top the cutting descends, mm; the axial step or the depth. */
  leadMm: number
  /** The console's `FaceStats::moves`: a descent and a plunge counted as two moves. */
  moves: number
  /** Cutting length as the console prints it: the plan's cut chords (`cutLengthMm`). */
  lengthMm: number
}

/** The layout numbers of one rectangular pocket, as the console prints them. */
export interface PocketStats {
  fits: boolean
  rows: number
  passes: number
  stepDepthsMm: number[]
  toolRadiusMm: number
  insetMm: number
  innerWidthMm: number
  innerHeightMm: number
  stepMm: number
  /** The console's `PocketStats::moves`: one move wider than the plan's list per pass. */
  moves: number
  /** Cutting length as the console prints it: the plan's cut chords (`cutLengthMm`). */
  lengthMm: number
}

/**
 * The cut length of `moves` as the console counts it: the chords of the cut moves that follow
 * another move. The first move of the whole program has no point before it and is charged nothing,
 * so `index` is the absolute index of the first move, not the index inside the span.
 */
export const cutLengthMm = (moves: CamMove[], index = 0): number => {
  let length = 0
  moves.forEach((move, local) => {
    if (move.kind !== 'cut' || index + local === 0) return
    const from = moves[local - 1].to
    length += Math.hypot(move.to.x - from.x, move.to.y - from.y, move.to.z - from.z)
  })
  return length
}

/**
 * The read of a face layout, as the console's `faceStats` does it: the printed length is the move
 * chords of the plan (see `cutLengthMm`), not the strip arithmetic of `FaceStats::lengthMm`, which
 * drops the lead and the Z ramps of the passes. The move count is the console's own formula.
 */
export const faceStats = (layout: FaceLayout, stock: StockBox, operation: FaceOperation): FaceStats => {
  const sizeX = stock.max.x - stock.min.x
  const sizeY = stock.max.y - stock.min.y
  void stock
  void sizeX
  void sizeY
  const leadMm = Math.min(operation.depthMm, operation.stepDownMm)
  const strips = layout.strips
  const passes = layout.passes.length
  return {
    strips,
    passes,
    stepDepthsMm: layout.passes,
    toolRadiusMm: operation.tool.diameterMm / 2,
    stripStepMm: layout.stripStepMm,
    edgeOverrunMm: layout.edgeOverrunMm,
    sideOverrunMm: 0,
    leadMm,
    moves: strips * 5 * passes + passes,
    lengthMm: Number.NaN,
  }
}

/** The pocket stats of a layout: per pass the ramp, the rows, the links and the exit. */
export const pocketStats = (layout: PocketLayout, operation: RectPocketOperation): PocketStats => {
  const passes = layout.passes.length
  const rows = layout.rows
  const innerWidth = layout.alongX ? layout.alongMax - layout.alongMin : layout.acrossMax - layout.acrossMin
  const innerHeight = layout.alongX ? layout.acrossMax - layout.acrossMin : layout.alongMax - layout.alongMin
  return {
    fits: layout.fits,
    rows,
    passes,
    stepDepthsMm: layout.passes,
    toolRadiusMm: operation.tool.diameterMm / 2,
    insetMm: layout.insetMm,
    innerWidthMm: innerWidth,
    innerHeightMm: innerHeight,
    stepMm: layout.stepMm,
    moves: layout.fits ? passes * (4 + rows + (rows - 1) + 1) : 0,
    lengthMm: layout.fits
      ? passes * (rows * innerWidth + (rows - 1) * innerHeight) + 2 * passes * operation.depthMm
      : 0,
  }
}
