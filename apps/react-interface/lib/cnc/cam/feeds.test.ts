// The cutting-data table: every row, the formulas from calc.ts and the source line. The
// reference numbers are the console output of `apps/quickprog` for the same parameters
// (`--op face --material steel`, `--op drill --drill-kind carbide`, ...), not a self-check.
import { describe, expect, it } from 'vitest'

import {
  CUTTING_TABLES,
  type DrillBody,
  drillBodyName,
  feedsForDrill,
  feedsForMill,
  materialName,
  parseDrillBody,
  parseMaterial,
  type Material,
} from './feeds'

describe('mill table', () => {
  it('has both rows with the demo numbers', () => {
    expect(CUTTING_TABLES.mill).toEqual([
      { material: 'aluminium', tool: 'flat', vc: 150, fz: 0.06 },
      { material: 'steel', tool: 'flat', vc: 90, fz: 0.03 },
    ])
  })

  it('matches the demo for aluminium flat 10', () => {
    expect(feedsForMill('aluminium', 10)).toEqual({
      rpm: 4775,
      feedMmMin: 860,
      plungeFeedMmMin: 215,
      source: 'таблица: алюминий, ф10',
    })
  })

  it('matches the demo for steel flat 10 and steel flat 8', () => {
    expect(feedsForMill('steel', 10)).toEqual({
      rpm: 2865,
      feedMmMin: 258,
      plungeFeedMmMin: 65,
      source: 'таблица: сталь, ф10',
    })
    expect(feedsForMill('steel', 8)).toEqual({
      rpm: 3581,
      feedMmMin: 322,
      plungeFeedMmMin: 81,
      source: 'таблица: сталь, ф8',
    })
  })

  it('matches the demo for steel flat 6 and aluminium flat 12', () => {
    expect(feedsForMill('steel', 6)).toMatchObject({ rpm: 4775, feedMmMin: 430, plungeFeedMmMin: 108 })
    expect(feedsForMill('aluminium', 12)).toMatchObject({ rpm: 3979, feedMmMin: 716, plungeFeedMmMin: 179 })
  })

  it('gives zeros and no source for a non-positive diameter', () => {
    expect(feedsForMill('aluminium', 0)).toEqual({ rpm: 0, feedMmMin: 0, plungeFeedMmMin: 0, source: '' })
    expect(feedsForMill('steel', -3)).toEqual({ rpm: 0, feedMmMin: 0, plungeFeedMmMin: 0, source: '' })
  })
})

describe('drill table', () => {
  it('has all four rows with the demo numbers', () => {
    expect(CUTTING_TABLES.drill).toEqual([
      { material: 'aluminium', body: 'hss', vc: 40, k: 0.02 },
      { material: 'aluminium', body: 'carbide', vc: 120, k: 0.03 },
      { material: 'steel', body: 'hss', vc: 20, k: 0.015 },
      { material: 'steel', body: 'carbide', vc: 70, k: 0.02 },
    ])
  })

  const cases: { material: Material; body: DrillBody; d: number; rpm: number; feed: number; source: string }[] = [
    { material: 'aluminium', body: 'hss', d: 6, rpm: 2122, feed: 255, source: 'таблица: сверло HSS, алюминий, ф6' },
    {
      material: 'aluminium',
      body: 'carbide',
      d: 6,
      rpm: 6366,
      feed: 1146,
      source: 'таблица: сверло карбид, алюминий, ф6',
    },
    { material: 'steel', body: 'hss', d: 5, rpm: 1273, feed: 95, source: 'таблица: сверло HSS, сталь, ф5' },
    { material: 'steel', body: 'hss', d: 6, rpm: 1061, feed: 95, source: 'таблица: сверло HSS, сталь, ф6' },
    {
      material: 'steel',
      body: 'carbide',
      d: 3.5,
      rpm: 6366,
      feed: 446,
      source: 'таблица: сверло карбид, сталь, ф3.5',
    },
  ]

  for (const c of cases) {
    it(`matches the demo for ${c.source}`, () => {
      const row = feedsForDrill(c.material, c.body, c.d)
      expect(row.rpm).toBe(c.rpm)
      expect(row.feedMmMin).toBe(c.feed)
      expect(row.source).toBe(c.source)
      // A drill has one feed: the plunge is the working feed of the cycle.
      expect(row.plungeFeedMmMin).toBe(c.feed)
    })
  }

  it('gives zeros for a non-positive diameter', () => {
    expect(feedsForDrill('aluminium', 'hss', 0)).toEqual({
      rpm: 0,
      feedMmMin: 0,
      plungeFeedMmMin: 0,
      source: '',
    })
  })
})

describe('names and words', () => {
  it('names the material and the drill body as the demo does', () => {
    expect(materialName('aluminium')).toBe('алюминий')
    expect(materialName('steel')).toBe('сталь')
    expect(drillBodyName('hss')).toBe('HSS')
    expect(drillBodyName('carbide')).toBe('карбид')
  })

  it('parses the .qprog words', () => {
    expect(parseMaterial('aluminium')).toBe('aluminium')
    expect(parseMaterial('steel')).toBe('steel')
    expect(parseMaterial('titanium')).toBeNull()
    expect(parseDrillBody('hss')).toBe('hss')
    expect(parseDrillBody('carbide')).toBe('carbide')
    expect(parseDrillBody('diamond')).toBeNull()
  })
})
