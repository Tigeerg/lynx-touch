// The rest planner (PLAN-QUICKPROG 7.6, Q5): the raster of what the earlier pockets left.
//
// The cells of the rest (`restAreas`) are decomposition rectangles, not a second geometry: a cell is
// cut by rows along its long side, as the plain pocket is, but the mill centre inset is `D / 2 +
// allowanceMm` and every row is clipped to the cell, so a row never enters the area a previous
// pocket took. Two pieces of one row and two cells are linked by a rapid at the material top and a
// plunge of its own, the way the island pocket links its pieces: the material between two cells is
// already removed, so a cut across it would be a cut in air.
//
// A pass: rapid at safe Z over the start of the first cell, rapid down to the top, plunge to the
// plane, the cells, a cut back up to the top, then the next pass deeper; the last pass is the floor.
// When nothing is left the planner returns empty cells and adds no move: the save still goes on and
// the check marks the operation as an empty one, instead of a silent file with no toolpath.
import type { Operation, QuickProgram } from './ops'
import { axialPasses } from './path-face'
import type { Vec2 } from './contour-offset'
import { EPS, PathBuilder } from './path-builder'
import type { Area } from './rest-areas'
import { restAreas } from './rest-areas'

/** One piece of a row: the mill centre from one end to the other, mm. */
export interface RestPiece {
  from: Vec2
  to: Vec2
}

/** The rest layout: the cells to cut, the pieces of every cell and the axial passes. */
export interface RestLayout {
  /** False when the mill with the allowance fits no cell: nothing is planned. */
  fits: boolean
  /** The cells of the rest, mm; empty when the earlier pockets took the whole footprint. */
  cells: Area[]
  /** The pieces of every cell, in the order of the walk. */
  pieces: RestPiece[][]
  /** The Z of each pass, top down; the last is exactly the floor. */
  passes: number[]
  /** The area one cell cut, `sum(w * h)`, mm^2: what the operator reads as "left to take". */
  areaMm2: number
}

const rest = (op: Operation): Extract<Operation, { kind: 'rest' }> => {
  if (op.kind !== 'rest') throw new Error(`planRest called with a ${op.kind} operation`)
  return op
}

/** The rows of one cell: pieces along the long side, inset by `D / 2 + allowance`, clipped to it. */
const cellPieces = (cell: Area, inset: number, stepMm: number): RestPiece[] => {
  const innerW = cell.w - 2 * inset
  const innerH = cell.h - 2 * inset
  if (innerW < -EPS || innerH < -EPS) return []
  const alongX = cell.w >= cell.h
  const along = alongX ? innerW : innerH
  const across = alongX ? innerH : innerW
  const alongLow = (alongX ? cell.x : cell.y) + inset
  const acrossLow = (alongX ? cell.y : cell.x) + inset
  const nominal = Math.min(stepMm, across)
  const rows = nominal <= EPS || across <= EPS ? 1 : Math.ceil(across / nominal - EPS) + 1
  const step = rows > 1 ? across / (rows - 1) : 0
  const point = (a: number, b: number): Vec2 => (alongX ? { x: a, y: b } : { x: b, y: a })
  const pieces: RestPiece[] = []
  for (let i = 0; i < rows; i += 1) {
    const across = acrossLow + step * i
    const from = point(alongLow, across)
    const to = point(alongLow + along, across)
    pieces.push(i % 2 === 0 ? { from, to } : { from: to, to: from })
  }
  return pieces
}

/** The layout of a rest operation; a cell narrower than the mill body plans nothing. */
export function restLayout(program: QuickProgram, operation: Extract<Operation, { kind: 'rest' }>): RestLayout {
  const op = rest(operation)
  const inset = op.tool.diameterMm / 2 + op.allowanceMm
  const stepMm = op.tool.diameterMm * (op.stepOverPct / 100)
  const cells = restAreas(op, program.operations, program.stock)
  const pieces = cells.map((cell) => cellPieces(cell, inset, stepMm))
  const areaMm2 = cells.reduce((sum, cell) => sum + cell.w * cell.h, 0)
  const passes = axialPasses(op.topZ, op.depthMm, op.stepDownMm)
  return { fits: pieces.some((list) => list.length > 0), cells, pieces, passes, areaMm2 }
}

/** The rest of one operation, appended to `out`; nothing left plans no move at all. */
export function planRest(program: QuickProgram, operation: Operation, out: PathBuilder): RestLayout {
  const op = rest(operation)
  const layout = restLayout(program, op)
  if (!layout.fits) return layout
  const top = op.topZ
  const safeZ = program.safeZ
  const point = (xy: Vec2, z: number) => ({ x: xy.x, y: xy.y, z })
  const first = layout.pieces.find((list) => list.length > 0)?.[0]
  if (first === undefined) return layout
  layout.passes.forEach((planeZ) => {
    out.add('rapid', point(first.from, safeZ))
    out.add('rapid', point(first.from, top))
    out.add('cut', point(first.from, planeZ), op.cutting.plungeFeedMmMin)
    let end = first.from
    layout.pieces.forEach((list) => {
      for (const piece of list) {
        if (piece.from.x !== end.x || piece.from.y !== end.y) {
          // A rapid over the already removed top between two cells, then a plunge of its own.
          out.add('rapid', point(piece.from, top))
          out.add('cut', point(piece.from, planeZ), op.cutting.plungeFeedMmMin)
        }
        out.add('cut', point(piece.to, planeZ), op.cutting.feedMmMin)
        end = piece.to
      }
    })
    out.add('cut', point(end, top), op.cutting.plungeFeedMmMin)
  })
  return layout
}
