// The pocket with an island (PLAN-QUICKPROG 7.6): the raster of a pocket minus the island.
//
// The raster is the one of a plain pocket (`pocketLayout` in `path-pocket.ts`): rows along the long
// side, inset by `D / 2 + finishAllowance`, so the row count is the same as without the island. What
// changes is one row: the piece of the row inside the island zone is dropped. The zone is the island
// grown by `D / 2 + islandAllowance`, the place the mill centre may not enter; a row that crosses it
// is cut in two pieces that end on the zone edge, and a row that misses it stays one strip.
//
// The link between two pieces of a row is a rapid over the island at the top of the material: the top
// is already cut (the raster is one Z pass or several), the lift is a millimetre or two and the tool
// never stands inside the island zone, so a cut along the floor across the island is not possible and
// a safe-Z lift for every row would double the air time. The finish pass around the island comes last,
// at the same plane, on the grown outline; its corners are the ones of the island rectangle.
import type { IslandPocketOperation, Operation, QuickProgram, Rect } from './ops'
import type { Vec2 } from './contour-offset'
import { axialPasses } from './path-face'
import { pocketLayout } from './path-pocket'
import type { PocketLayout } from './path-pocket'
import { EPS, PathBuilder } from './path-builder'

/** One piece of a row: the mill centre from one end to the other, mm. */
export interface RowPiece {
  from: Vec2
  to: Vec2
}

/** A row of the raster: the across coordinate and the pieces it is cut in. */
export interface IslandRow {
  across: number
  pieces: RowPiece[]
}

/** The layout of an island pocket: the raster frame, the rows cut around the island, the outline. */
export interface IslandLayout extends PocketLayout {
  /** The raster rows in the order of the walk, cut around the island. */
  rowPieces: IslandRow[]
  /** The island grown by `D / 2 + islandAllowance`: the zone the mill centre keeps out of. */
  zone: Rect
  /** The finish pass around the island: the grown outline, closed on its first point. */
  outline: Vec2[]
}

const islandPocket = (op: Operation): IslandPocketOperation => {
  if (op.kind !== 'pocket-island') throw new Error(`planIslandPocket called with a ${op.kind} operation`)
  return op
}

/** The layout of the operation; a tool that does not fit the pocket plans nothing. */
export function islandPocketLayout(operation: IslandPocketOperation): IslandLayout {
  const op = islandPocket(operation)
  const frame = pocketLayout({ ...op, kind: 'pocket', cornerRadiusMm: 0, finishAllowanceMm: 0 })
  const grow = op.tool.diameterMm / 2 + op.islandAllowanceMm
  const zone = {
    x: op.island.x - grow,
    y: op.island.y - grow,
    w: op.island.w + 2 * grow,
    h: op.island.h + 2 * grow,
  }
  const outline: Vec2[] = [
    { x: op.island.x - grow, y: op.island.y - grow },
    { x: op.island.x + op.island.w + grow, y: op.island.y - grow },
    { x: op.island.x + op.island.w + grow, y: op.island.y + op.island.h + grow },
    { x: op.island.x - grow, y: op.island.y + op.island.h + grow },
    { x: op.island.x - grow, y: op.island.y - grow },
  ]
  const passes = axialPasses(op.topZ, op.depthMm, op.stepDownMm)
  if (!frame.fits) return { ...frame, passes, rowPieces: [], zone, outline }
  const rows: IslandRow[] = []
  // The zone across the raster: the rows run along `along`, the step is `across`.
  const acrossLow = frame.alongX ? zone.y : zone.x
  const acrossHigh = frame.alongX ? zone.y + zone.h : zone.x + zone.w
  const alongLow = frame.alongX ? zone.x : zone.y
  const alongHigh = frame.alongX ? zone.x + zone.w : zone.y + zone.h
  for (let i = 0; i < frame.rows; i += 1) {
    const across = frame.acrossMin + frame.stepMm * i
    const rowPoint = (along: number): Vec2 => (frame.alongX ? { x: along, y: across } : { x: across, y: along })
    const pieces: RowPiece[] = []
    if (across < acrossLow - EPS || across > acrossHigh + EPS) {
      pieces.push({ from: rowPoint(frame.alongMin), to: rowPoint(frame.alongMax) })
    } else {
      if (alongLow - frame.alongMin > EPS) {
        pieces.push({ from: rowPoint(frame.alongMin), to: rowPoint(alongLow) })
      }
      if (frame.alongMax - alongHigh > EPS) {
        pieces.push({ from: rowPoint(alongHigh), to: rowPoint(frame.alongMax) })
      }
    }
    rows.push({ across, pieces })
  }
  return { ...frame, passes, rowPieces: rows, zone, outline }
}

/** The pieces of one row in the direction of the walk; `forward` is the row order of the raster. */
const walkRow = (row: IslandRow, forward: boolean): RowPiece[] =>
  forward ? row.pieces : [...row.pieces].reverse().map((piece) => ({ from: piece.to, to: piece.from }))

/**
 * The island pocket of one operation, appended to `out`: the raster pieces row by row, then the
 * finish pass around the island. Every Z pass repeats the same rows one step deeper.
 */
export function planIslandPocket(program: QuickProgram, operation: Operation, out: PathBuilder): IslandLayout {
  const op = islandPocket(operation)
  const layout = islandPocketLayout(op)
  if (!layout.fits) return layout
  const top = op.topZ
  const safeZ = program.safeZ
  const point = (xy: Vec2, z: number) => ({ x: xy.x, y: xy.y, z })
  const first = layout.rowPieces[0]?.pieces[0]
  if (first === undefined) return layout
  const entry = point(first.from, top)
  layout.passes.forEach((planeZ) => {
    out.add('rapid', point(first.from, safeZ))
    out.add('rapid', entry)
    out.add('cut', point(first.from, planeZ), op.cutting.plungeFeedMmMin)
    let forward = true
    let end = first.from
    layout.rowPieces.forEach((row) => {
      const pieces = walkRow(row, forward)
      pieces.forEach((piece) => {
        if (piece.from.x !== end.x || piece.from.y !== end.y) {
          // A rapid over the already cut top: the floor between two pieces is open material.
          out.add('rapid', point(piece.from, top))
          out.add('cut', point(piece.from, planeZ), op.cutting.plungeFeedMmMin)
        }
        out.add('cut', point(piece.to, planeZ), op.cutting.feedMmMin)
        end = piece.to
      })
      forward = !forward
    })
    out.add('cut', point(end, top), op.cutting.plungeFeedMmMin)
    // The finish pass around the island on its own plane, closed on its first corner.
    const start = layout.outline[0]
    out.add('rapid', point(start, top))
    out.add('cut', point(start, planeZ), op.cutting.plungeFeedMmMin)
    for (const corner of layout.outline.slice(1)) out.add('cut', point(corner, planeZ), op.cutting.feedMmMin)
    out.add('cut', point(start, top), op.cutting.plungeFeedMmMin)
  })
  return layout
}
