// Cutting data from the material, the diameter and the tool kind (PLAN-QUICKPROG, 7.4).
//
// This is the single home of the numbers: the C++ demo carries its own copy and the two must
// print the same mode line, which `feeds.test.ts` checks against the console output of the demo.
// The mill rows go through the formulas of `../calc` (the panel calculator), the drill rows
// through the same speed formula and `F = k * D * n`, so the panel has one arithmetic, not two.
//
// A row is per material and per tool, never per diameter: the diameter only enters the formula.
// The flutes belong to the tool kind (three for a flat end mill, as in the calculator defaults).
// The source line is Russian and short, as the console and the summary write it:
// `таблица: алюминий, ф10` and `таблица: сверло HSS, сталь, ф10`.
import { cuttingFeed, cuttingRpm } from '../calc'
import type { Cutting } from './ops'

export type Material = 'aluminium' | 'steel'
export type MillKind = 'flat'
export type DrillBody = 'hss' | 'carbide'

/** Cutting data of one tool: the speed, the feed and the plunge, plus where the row came from. */
export interface Feeds {
  rpm: number
  feedMmMin: number
  plungeFeedMmMin: number
  /** Short Russian line: the table, the material and the diameter. Empty for a bad diameter. */
  source: string
}

/** One row of the mill table: Vc in m/min and the feed per tooth in mm/tooth. */
export interface MillRow {
  material: Material
  tool: MillKind
  vc: number
  fz: number
}

/** One row of the drill table: Vc in m/min and the feed per turn as a fraction of the diameter. */
export interface DrillRow {
  material: Material
  body: DrillBody
  vc: number
  k: number
}

/**
 * The tables, in the order of the demo. The aluminium flat row is the calculator default
 * (vc 150, fz 0.06); the other mill row and the four drill rows are handbook bands oriented to a
 * small stand. The demo keeps its own copy, so a change here wants a matching demo edit and the
 * reference numbers of `feeds.test.ts` re-taken from the console.
 */
export const CUTTING_TABLES: { mill: readonly MillRow[]; drill: readonly DrillRow[] } = Object.freeze({
  mill: Object.freeze([
    { material: 'aluminium', tool: 'flat', vc: 150, fz: 0.06 },
    { material: 'steel', tool: 'flat', vc: 90, fz: 0.03 },
  ] satisfies MillRow[]),
  drill: Object.freeze([
    { material: 'aluminium', body: 'hss', vc: 40, k: 0.02 },
    { material: 'aluminium', body: 'carbide', vc: 120, k: 0.03 },
    { material: 'steel', body: 'hss', vc: 20, k: 0.015 },
    { material: 'steel', body: 'carbide', vc: 70, k: 0.02 },
  ] satisfies DrillRow[]),
})

/** Flutes per mill kind: a flat end mill takes three, as in the calculator defaults. */
const MILL_FLUTES: Record<MillKind, number> = { flat: 3 }

/** Z plunge feed: a quarter of the XY feed, the usual conservative rule of an end mill. */
const PLUNGE_SHARE = 4

const EMPTY: Feeds = { rpm: 0, feedMmMin: 0, plungeFeedMmMin: 0, source: '' }

/**
 * The text of a number the source line and the reasons print: the console `%g` spelling, so a
 * float sum does not leak as `18.200000000000003`.
 */
export const fmt = (value: number): string => {
  if (!Number.isFinite(value) || value === 0) return '0'
  return String(Number(value.toPrecision(6)))
}

/** Russian name of the material, as the console and the panel write it. */
export const materialName = (material: Material): string => {
  switch (material) {
    case 'aluminium':
      return 'алюминий'
    case 'steel':
      return 'сталь'
  }
}

/** Russian name of the drill body material: "HSS" or "карбид". */
export const drillBodyName = (body: DrillBody): string => {
  switch (body) {
    case 'hss':
      return 'HSS'
    case 'carbide':
      return 'карбид'
  }
}

/** The `.qprog` and console word of the material; null for anything else. */
export const parseMaterial = (text: string): Material | null => {
  if (text === 'aluminium' || text === 'steel') return text
  return null
}

/** The `.qprog` and console word of the drill body; null for anything else. */
export const parseDrillBody = (text: string): DrillBody | null => {
  if (text === 'hss' || text === 'carbide') return text
  return null
}

const shape = (rpm: number, feedMmMin: number, source: string): Feeds => ({
  rpm,
  feedMmMin,
  plungeFeedMmMin: Math.round(feedMmMin / PLUNGE_SHARE),
  source,
})

/**
 * Cutting data for a flat end mill. A non-positive diameter gives zeros and an empty source: the
 * caller validates the diameter first and must not plan a pass from a zero.
 */
export const feedsForMill = (material: Material, diameterMm: number): Feeds => {
  if (diameterMm <= 0) return EMPTY
  const row = CUTTING_TABLES.mill.find((r) => r.material === material && r.tool === 'flat')
  if (row === undefined) return EMPTY
  const rpm = cuttingRpm(row.vc, diameterMm)
  const feedMmMin = cuttingFeed(row.fz, MILL_FLUTES.flat, rpm)
  return shape(rpm, feedMmMin, `таблица: ${materialName(material)}, ф${fmt(diameterMm)}`)
}

/**
 * Cutting data of a drill of the given body material: `n = 1000 * Vc / (pi * D)` and
 * `F = k * D * n`. A drill has one feed, so the plunge equals the working feed.
 */
export const feedsForDrill = (material: Material, body: DrillBody, diameterMm: number): Feeds => {
  if (diameterMm <= 0) return EMPTY
  const row = CUTTING_TABLES.drill.find((r) => r.material === material && r.body === body)
  if (row === undefined) return EMPTY
  const rpm = cuttingRpm(row.vc, diameterMm)
  const feedMmMin = Math.round(row.k * diameterMm * rpm)
  const source = `таблица: сверло ${drillBodyName(body)}, ${materialName(material)}, ф${fmt(diameterMm)}`
  return { rpm, feedMmMin, plungeFeedMmMin: feedMmMin, source }
}

/** Cutting data of the table for a mill, or for a drill of the given body material. */
export const cuttingFor = (material: Material, diameterMm: number, body: DrillBody | null = null): Cutting => {
  const row = body === null ? feedsForMill(material, diameterMm) : feedsForDrill(material, body, diameterMm)
  return { spindleRpm: row.rpm, feedMmMin: row.feedMmMin, plungeFeedMmMin: row.plungeFeedMmMin }
}

/** The calculator formulas for one row of the mill table, in the shape of `Cutting`. */
export const millCutting = (vc: number, fz: number, flutes: number, diameterMm: number): Cutting => {
  const rpm = cuttingRpm(vc, diameterMm)
  const feedMmMin = cuttingFeed(fz, flutes, rpm)
  return { spindleRpm: rpm, feedMmMin, plungeFeedMmMin: Math.round(feedMmMin / 4) }
}
