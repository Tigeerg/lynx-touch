// The area the earlier pockets took out (PLAN-QUICKPROG 7.6, Q5, rest machining), no simulation.
//
// A pocket of the document is its footprint and not its trajectory: the mill disc is not swept along
// the raster, so the area is the rectangle of the pocket (`pocket`, `pocket-island`) or the box of
// its circle (`circle-pocket`), and a slot contributes the box of its two ends grown by half its
// width (a widened disk). The taken area is a decomposition of the plane by every x and every y
// bound of the boxes: the cells are the rectangles between neighbouring bounds, each one either
// fully inside some box or fully outside, so the area is exact; a circle-pocket and a slot are the
// two approximations, their boxes, and both are announced (`approximationOf`).
//
// The rest is the footprint minus the taken area (`subtractAreas`), and the raster walk plans per
// cell of that difference: a row of the raster inside one cell stays one move, and a row cut in two
// by a taken pocket becomes two moves with a rapid over the cell between them.
import type { Operation, Rect } from './ops'

/** One box of the union, mm; `x`/`y` is the lower-left corner, as of every rectangle here. */
export type Area = Rect

/** The approximation in play: the box of a circle or of a slot, or none. */
export type Approximation = 'circle' | 'slot' | null

export const REST_EPS = 1e-9

/** True when `outer` contains `inner` with the tolerance, for the cell test. */
const inside = (outer: Area, inner: Area): boolean =>
  inner.x >= outer.x - REST_EPS &&
  inner.y >= outer.y - REST_EPS &&
  inner.x + inner.w <= outer.x + outer.w + REST_EPS &&
  inner.y + inner.h <= outer.y + outer.h + REST_EPS

/**
 * The area to cut: `all` minus `gone`. Both lists are decomposed by every x and y bound, every cell
 * is asked whether it stands inside an `all` box and in no `gone` box, and the kept cells of a row
 * merge into one rectangle. A footprint the same box took is one `gone` box, so the difference is
 * empty without a special case; a pocket inside the footprint leaves four slabs, exactly.
 */
export function subtractAreas(all: Area[], gone: Area[]): Area[] {
  const kept: Area[] = []
  for (const box of all) if (box.w > REST_EPS && box.h > REST_EPS) kept.push(box)
  const off: Area[] = []
  for (const box of gone) if (box.w > REST_EPS && box.h > REST_EPS) off.push(box)
  if (kept.length === 0) return []
  // The grid carries the bounds of all boxes: a corner of a taken pocket has to be a corner of a
  // cell, so a cell never mixes the inside and the outside of that pocket.
  const xLines: number[] = []
  const yLines: number[] = []
  for (const box of kept) {
    xLines.push(box.x, box.x + box.w)
    yLines.push(box.y, box.y + box.h)
  }
  for (const box of off) {
    xLines.push(box.x, box.x + box.w)
    yLines.push(box.y, box.y + box.h)
  }
  const xs = lines(xLines)
  const ys = lines(yLines)
  const out: Area[] = []
  for (let row = 0; row + 1 < ys.length; row += 1) {
    const y = ys[row]
    const h = ys[row + 1] - y
    let runStart: number | null = null
    let runEnd = 0
    for (let column = 0; column + 1 < xs.length; column += 1) {
      const x = xs[column]
      const w = xs[column + 1] - x
      const cell: Area = { x, y, w, h }
      let solid = false
      for (const box of kept) if (inside(box, cell)) solid = true
      let free = solid
      if (free) for (const box of off) if (inside(box, cell)) free = false
      if (free) {
        if (runStart === null) runStart = x
        runEnd = x + w
      } else if (runStart !== null) {
        out.push({ x: runStart, y, w: runEnd - runStart, h })
        runStart = null
      }
    }
    if (runStart !== null) out.push({ x: runStart, y, w: runEnd - runStart, h })
  }
  return out
}

/** The distinct sorted bounds of a list of boxes on one axis; a bound is one grid line. */
const lines = (values: number[]): number[] => {
  const sorted = [...values].sort((a, b) => a - b)
  const out: number[] = []
  for (const value of sorted) {
    if (out.length === 0 || value - out[out.length - 1] > REST_EPS) out.push(value)
  }
  return out
}

/**
 * The union of the boxes: the cells of `subtractAreas` with nothing taken. Horizontally neighbouring
 * cells merge into one rectangle, so a plain pocket is one area and a slab between two boxes is one
 * row of cells.
 */
export function unionAreas(boxes: Area[]): Area[] {
  return subtractAreas(boxes, [])
}

/**
 * The area a document operation removed, or null when the kind cuts no pocket. A source whose
 * operation is disabled removed nothing: it is skipped by the caller as well.
 */
export function removedArea(op: Operation): Area | null {
  switch (op.kind) {
    case 'pocket':
      return { ...op.rect }
    case 'pocket-island':
      return { ...op.rect }
    case 'circle-pocket':
      return { x: op.circle.x - op.circle.r, y: op.circle.y - op.circle.r, w: 2 * op.circle.r, h: 2 * op.circle.r }
    case 'slot': {
      // A slot is the mill walking the segment: the ends are discs of `width / 2` (a slot no wider
      // than the mill is one pass and its ends are half a diameter), the box covers both and the
      // widened middle. The box is the one approximation of a slot.
      const half = op.widthMm / 2
      const x = Math.min(op.from.x, op.to.x) - half
      const y = Math.min(op.from.y, op.to.y) - half
      return {
        x,
        y,
        w: Math.abs(op.to.x - op.from.x) + 2 * half,
        h: Math.abs(op.to.y - op.from.y) + 2 * half,
      }
    }
    default:
      return null
  }
}

/** The rectangle bounds a stock names: the min and max corners whatever their order. */
export interface StockPlan {
  min: { x: number; y: number }
  max: { x: number; y: number }
}

/** The footprint of a rest operation: its rectangle, or the whole stock when it names none. */
export function footprint(op: Extract<Operation, { kind: 'rest' }>, stock: StockPlan): Area {
  if (op.rect.w > REST_EPS && op.rect.h > REST_EPS) return { ...op.rect }
  return {
    x: Math.min(stock.min.x, stock.max.x),
    y: Math.min(stock.min.y, stock.max.y),
    w: Math.abs(stock.max.x - stock.min.x),
    h: Math.abs(stock.max.y - stock.min.y),
  }
}

/** The area a rest operation has to cut: the footprint minus the union of its sources. */
export function restAreas(
  operation: Extract<Operation, { kind: 'rest' }>,
  program: Operation[],
  stock: StockPlan,
): Area[] {
  const taken: Area[] = []
  for (const index of operation.source) {
    const source = program[index]
    if (source === undefined || !source.enabled) continue
    const area = removedArea(source)
    if (area !== null) taken.push(area)
  }
  // The difference of the footprint and the taken area, not a filter of the footprint alone: a
  // pocket that took the whole footprint leaves an empty area instead of "a box inside a box".
  return subtractAreas([footprint(operation, stock)], taken)
}

/** The word of the approximation a list of sources brings in, or null when the area is exact. */
export function approximationOf(program: Operation[], source: number[]): Approximation {
  const kinds = source.map((index) => program[index]?.kind)
  if (kinds.includes('circle-pocket')) return 'circle'
  if (kinds.includes('slot')) return 'slot'
  return null
}
