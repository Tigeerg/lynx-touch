// The rectangular pocket by a raster (PLAN-QUICKPROG, 7.4 and 10), the port of `path_pocket.cpp`.
//
// The raster frame: rows run along the long side of the pocket and step across the short one. The
// inner boundary is the pocket rectangle shrunk by `inset = D / 2 + finishAllowance` on every side:
// the last row centre is half a diameter from the wall and the allowance is the stock left on it.
// The floor carries no allowance: the last Z pass is the final depth.
//
// The rows cover the inner rectangle exactly: `rows = ceil(across / nominal) + 1` with
// `nominal = min(D * stepOverPct / 100, across)`, so the real step never exceeds the nominal one and
// the first and last rows stand on the boundary. The raster alternates direction; the link between
// two rows is a cut across the step at the pass Z, over the already removed material.
//
// A pass: rapid at safe Z over the start of the first row, rapid down to the material top, plunge to
// the pass Z, the rows, a cut back up to the top, then the next pass. The last pass is the bottom.
import type { Operation, QuickProgram, RectPocketOperation } from './ops'
import { EPS, PathBuilder } from './path-builder'
import { axialPasses } from './path-face'

/** The pocket raster layout: the inner rectangle, the row count and the passes. */
export interface PocketLayout {
  /** False when the tool does not fit: the inner rectangle is not wider than a point. */
  fits: boolean
  /** True when the rows run along X (the pocket is at least as wide as high). */
  alongX: boolean
  /** The tool centre inset from the wall, `D / 2 + finishAllowance`, mm. */
  insetMm: number
  alongMin: number
  alongMax: number
  acrossMin: number
  acrossMax: number
  rows: number
  /** Real step between two rows, mm; zero for a single row. */
  stepMm: number
  passes: number[]
}

const pocket = (op: Operation): RectPocketOperation => {
  if (op.kind !== 'pocket') throw new Error(`planRectPocket called with a ${op.kind} operation`)
  return op
}

/** The raster layout of a rectangular pocket. */
export function pocketLayout(operation: RectPocketOperation): PocketLayout {
  const rect = operation.rect
  const insetMm = operation.tool.diameterMm / 2 + operation.finishAllowanceMm
  const alongX = rect.w >= rect.h
  const along = alongX ? rect.w : rect.h
  const across = alongX ? rect.h : rect.w
  const acrossLow = alongX ? rect.y : rect.x
  const alongLow = alongX ? rect.x : rect.y
  const empty: PocketLayout = {
    fits: false,
    alongX,
    insetMm,
    alongMin: 0,
    alongMax: 0,
    acrossMin: 0,
    acrossMax: 0,
    rows: 0,
    stepMm: 0,
    passes: [],
  }
  const innerAlong = along - 2 * insetMm
  const innerAcross = across - 2 * insetMm
  if (innerAlong < -EPS || innerAcross < -EPS) return empty
  const nominal = Math.min(operation.tool.diameterMm * (operation.stepOverPct / 100), innerAcross)
  const rows = nominal <= EPS ? 1 : Math.ceil(innerAcross / nominal - EPS) + 1
  return {
    fits: true,
    alongX,
    insetMm,
    alongMin: alongLow + insetMm,
    alongMax: alongLow + along - insetMm,
    acrossMin: acrossLow + insetMm,
    acrossMax: acrossLow + across - insetMm,
    rows,
    stepMm: rows > 1 ? innerAcross / (rows - 1) : 0,
    passes: axialPasses(operation.topZ, operation.depthMm, operation.stepDownMm),
  }
}

/** The raster of one pocket, appended to `out`; a tool that does not fit plans nothing. */
export function planRectPocket(program: QuickProgram, operation: Operation, out: PathBuilder): PocketLayout {
  const op = pocket(operation)
  const layout = pocketLayout(op)
  if (!layout.fits) return layout
  const safeZ = program.safeZ
  const top = op.topZ
  const point = (along: number, across: number, z: number) =>
    layout.alongX ? { x: along, y: across, z } : { x: across, y: along, z }
  for (const planeZ of layout.passes) {
    const firstAcross = layout.acrossMin
    out.add('rapid', point(layout.alongMin, firstAcross, safeZ))
    out.add('rapid', point(layout.alongMin, firstAcross, top))
    out.add('cut', point(layout.alongMin, firstAcross, planeZ), op.cutting.plungeFeedMmMin)
    let along = layout.alongMin
    let across = firstAcross
    for (let i = 0; i < layout.rows; i += 1) {
      across = layout.acrossMin + layout.stepMm * i
      const target = i % 2 === 0 ? layout.alongMax : layout.alongMin
      out.add('cut', point(target, across, planeZ), op.cutting.feedMmMin)
      along = target
      if (i + 1 < layout.rows) {
        const nextAcross = layout.acrossMin + layout.stepMm * (i + 1)
        out.add('cut', point(along, nextAcross, planeZ), op.cutting.feedMmMin)
      }
    }
    out.add('cut', point(along, across, top), op.cutting.plungeFeedMmMin)
  }
  return layout
}
