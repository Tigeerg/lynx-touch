// A pocket with an island (`pocket-island`, PLAN-QUICKPROG 7.6): the raster with the island cut out.
//
// The raster is the one of a plain pocket (`pocketLayout`): rows along the long side, the inner
// rectangle inset by `D / 2 + finishAllowance`. Every row is cut into the pieces outside the island
// grown by `D / 2 + islandAllowance`; a piece that would cross that zone is dropped, and the gap of a
// row that misses the island entirely stays one piece, so the row count does not grow. The finish pass
// walks the island outline at the allowance distance. The island has to fit between the walls with a
// clearance of at least the mill diameter, or the operation is refused.
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { emitProgram } from './emit'
import { PathBuilder } from './path-builder'
import { buildProgram } from './path'
import { islandPocketLayout, planIslandPocket } from './path-pocket-island'
import { pocketLayout } from './path-pocket'
import type { IslandPocketOperation, QuickProgram, Rect } from './ops'
import { parseProgram, serializeProgram } from './serialize'
import { validate } from './validate'

const stock = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

/** A 60 x 40 pocket with a 20 x 20 island in its centre, an 8 mm mill. */
const island = (patch: Partial<IslandPocketOperation> = {}): IslandPocketOperation => ({
  ...(newOperation('pocket-island', { id: 'Остров', topZ: 0, depthMm: 4, stepDownMm: 2 }) as IslandPocketOperation),
  tool: { number: 1, diameterMm: 8, hNumber: 1 },
  rect: { x: -30, y: -20, w: 60, h: 40 },
  island: { x: -10, y: -10, w: 20, h: 20 },
  islandAllowanceMm: 0,
  stepOverPct: 70,
  ...patch,
})

/** A plain pocket with the same rectangle, for the row count of the reference. */
const plain = (op: IslandPocketOperation): Parameters<typeof pocketLayout>[0] =>
  ({
    ...op,
    kind: 'pocket',
    cornerRadiusMm: 0,
    finishAllowanceMm: 0,
  }) as Parameters<typeof pocketLayout>[0]

const program = (operations: QuickProgram['operations']): QuickProgram => ({
  ...newProgram(stock, 'ISLAND'),
  operations,
})

const plan = (op: IslandPocketOperation) => {
  const out = new PathBuilder()
  planIslandPocket(program([op]), op, out)
  return out.result()
}

/** The cut moves of a path, each from the point the tool stood at before it. */
const cutSegments = (
  moves: { kind: string; to: { x: number; y: number; z: number } }[],
): { from: { x: number; y: number; z: number }; to: { x: number; y: number; z: number } }[] => {
  const out: { from: { x: number; y: number; z: number }; to: { x: number; y: number; z: number } }[] = []
  moves.forEach((move, at) => {
    if (move.kind !== 'cut') return
    const from = at === 0 ? { x: 0, y: 0, z: 0 } : moves[at - 1].to
    out.push({ from, to: move.to })
  })
  return out
}

/** True when the straight cut `from -> to` enters the closed rectangle `zone` grown by `grow`. */
const crosses = (
  seg: { from: { x: number; y: number }; to: { x: number; y: number } },
  zone: Rect,
  grow: number,
): boolean => {
  const minX = zone.x - grow
  const maxX = zone.x + zone.w + grow
  const minY = zone.y - grow
  const maxY = zone.y + zone.h + grow
  // Liang-Barsky: the segment against the closed box. A touch counts as a crossing at the Z plane.
  const dx = seg.to.x - seg.from.x
  const dy = seg.to.y - seg.from.y
  let t0 = 0
  let t1 = 1
  const clip = (p: number, q: number): boolean => {
    if (Math.abs(p) < 1e-12) return q >= 0
    const r = q / p
    if (p < 0) {
      if (r > t1) return false
      if (r > t0) t0 = r
    } else {
      if (r < t0) return false
      if (r < t1) t1 = r
    }
    return true
  }
  return (
    clip(-dx, seg.from.x - minX) &&
    clip(dx, maxX - seg.from.x) &&
    clip(-dy, seg.from.y - minY) &&
    clip(dy, maxY - seg.from.y)
  )
}

describe('карман с островом: разрезка растра', () => {
  it('20x20 остров в 60x40 кармане: число ходовых полос меньше, чем без острова', () => {
    const op = island()
    const layout = islandPocketLayout(op)
    const reference = pocketLayout(plain(op))
    // The row exists with its holes: the pieces of the island rows are shorter than the full row.
    expect(layout.rowPieces.length).toBe(reference.rows)
    expect(reference.rows).toBeGreaterThanOrEqual(5)
    // The rows through the island lose their middle: the zone 4..44 mm across holds rows 8, 15.2,
    // 22.4 and 29.6 mm from the border; the last two rows of 10 and 5 mm miss it. Six rows of ten
    // strips: five of them are cut in two.
    const splitRows = layout.rowPieces.filter((row) => row.pieces.length > 1).length
    expect(splitRows).toBe(5)
    const cutMm = layout.rowPieces
      .flatMap((row) => row.pieces)
      .reduce((sum, piece) => sum + Math.abs(piece.to.x - piece.from.x), 0)
    const plainMm = reference.rows * Math.abs(reference.alongMax - reference.alongMin)
    // A full row is 52 mm of 60; every split row loses 28 mm of the island zone over the top.
    expect(cutMm).toBeLessThan(plainMm)
    expect(plainMm - cutMm).toBeCloseTo(5 * (20 + 2 * 4), 6)
  })

  it('ни один cut не входит в остров, расширенный на D/2 + припуск', () => {
    const op = island({ islandAllowanceMm: 0.5 })
    const cut = plan(op)
    const segs = cutSegments(cut.moves)
    const grow = op.tool.diameterMm / 2 + op.islandAllowanceMm
    const inside = segs.filter((seg) => crosses(seg, op.island, grow - 1e-6))
    expect(inside).toEqual([])
  })

  it('последний проход обходит остров по периметру с припуском', () => {
    const op = island({ islandAllowanceMm: 1 })
    const layout = islandPocketLayout(op)
    const offset = op.tool.diameterMm / 2 + op.islandAllowanceMm
    // The outline of the grown island: four corners, the first one closed again.
    expect(layout.outline).toEqual([
      { x: op.island.x - offset, y: op.island.y - offset },
      { x: op.island.x + op.island.w + offset, y: op.island.y - offset },
      { x: op.island.x + op.island.w + offset, y: op.island.y + op.island.h + offset },
      { x: op.island.x - offset, y: op.island.y + op.island.h + offset },
      { x: op.island.x - offset, y: op.island.y - offset },
    ])
  })

  it('остров 55 мм в кармане 60 фрезой 8 — зазор меньше фрезы', () => {
    const doc = program([island({ island: { x: -27.5, y: -27.5, w: 55, h: 55 } })])
    const checked = validate(doc)
    expect(checked.ok).toBe(false)
    const problem = checked.ok ? undefined : checked.bad.find((item) => item.field === 'island')
    expect(problem?.reason).toContain('Остров не помещается: зазор меньше фрезы')

    const outer = program([island({ island: { x: -60, y: -10, w: 20, h: 20 } })])
    const outside = validate(outer)
    expect(outside.ok).toBe(false)
    expect(outside.ok ? [] : outside.bad.map((item) => item.field)).toContain('island')
  })

  it('остров вне кармана: отрицательный зазор выбирается как наименьшая сторона', () => {
    // An island outside the pocket is refused with the clearance wording: `gapOf` reads the narrowest
    // of the four sides of the overlap, and an outside axis-aligned rectangle always has a negative
    // one (its wall stands past the pocket wall), so the refusal names the negative gap, not the
    // "outside the pocket" branch. The wide branch below it is unreachable for a consistent
    // rectangle; the case keeps the behaviour pinned so a change of the rule shows here.
    const doc = program([island({ island: { x: 40, y: -10, w: 20, h: 20 } })])
    const checked = validate(doc)
    expect(checked.ok).toBe(false)
    const problem = checked.ok ? undefined : checked.bad.find((item) => item.field === 'island')
    expect(problem?.reason).toContain('зазор меньше фрезы')
    expect(problem?.reason).not.toContain('выходит за')
  })

  it('программа печатает карман с островом, round-trip .qprog', () => {
    const doc = program([island({ islandAllowanceMm: 0.4, stepOverPct: 60 })])
    const planned = buildProgram(doc)
    expect(planned.ok).toBe(true)
    const text = planned.ok ? emitProgram(doc, planned.path).join('\n') : ''
    expect(text).toContain('(UI_OP: Остров)')
    const round = parseProgram(serializeProgram(doc))
    expect(round.ok).toBe(true)
    const back = round.ok ? round.program.operations[0] : null
    expect(back?.kind).toBe('pocket-island')
    const pocket = back?.kind === 'pocket-island' ? back : null
    expect(pocket?.rect).toEqual({ x: -30, y: -20, w: 60, h: 40 })
    expect(pocket?.island).toEqual({ x: -10, y: -10, w: 20, h: 20 })
    expect(pocket?.islandAllowanceMm).toBe(0.4)
    expect(pocket?.stepOverPct).toBe(60)
  })
})
