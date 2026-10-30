// The `.qprog` file of the panel and of the C++ demo (PLAN-QUICKPROG, 7.4, C3).
//
// The golden `.qprog` files in `etl/` are written by `apps/quickprog` for the same operations as the
// `.ngc` references; the command lines are in `etl/README.md`. Three checks hold the common format:
//
//   * demo file -> `parseProgram` -> `serializeProgram` is the file byte for byte;
//   * a panel document -> `serializeProgram` -> `parseProgram` gives the same document;
//   * a panel document -> `serializeProgram` is the demo writer's own text, so the C++ demo reads
//     it and prints the same `.ngc` (a hand run in the report, not here).
//
// The negative cases are the reader's job: an unknown key names the line, a missing required key
// names the field, a foreign schema is refused, and a file of the previous build is read with the
// defaults the writer of that build left out.
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

import { newOperation, newProgram } from './defaults'
import { effectiveH } from './emit-words'
import type { Operation, QuickProgram } from './ops'
import { parseProgram, serializeProgram } from './serialize'

const etl = (name: string): string => readFileSync(fileURLToPath(new URL(`./etl/${name}`, import.meta.url)), 'utf8')

const STOCK = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

const tool = (number: number, diameterMm: number, hNumber = number) => ({ number, diameterMm, hNumber })

const cutting = (spindleRpm: number, feedMmMin: number, plungeFeedMmMin: number) => ({
  spindleRpm,
  feedMmMin,
  plungeFeedMmMin,
})

/** The document of `demo.qprog`: a face and a pocket, the console command `--demo`. */
const demoProgram = (): QuickProgram => ({
  ...newProgram(STOCK, 'QUICKPROG DEMO'),
  operations: [
    {
      ...newOperation('face', { id: 'Плоскость', topZ: 0, depthMm: 2, stepDownMm: 2, tool: tool(2, 10) }),
      cutting: cutting(4775, 860, 215),
      z: -2,
    },
    {
      ...newOperation('pocket', { id: 'Карман 60x40', topZ: 0, depthMm: 6, stepDownMm: 2.5, tool: tool(3, 8) }),
      cutting: cutting(5968, 859, 215),
      rect: { x: -30, y: -20, w: 60, h: 40 },
      cornerRadiusMm: 3,
      finishAllowanceMm: 0.2,
    },
  ],
})

/** The document of `steel.qprog`: a steel drill with an explicit R and a peck. */
const steelProgram = (): QuickProgram => ({
  ...newProgram(STOCK, 'DRILL'),
  operations: [
    {
      ...newOperation('drill', {
        id: 'Отверстия',
        topZ: 0,
        depthMm: 3,
        tool: tool(8, 5),
        material: 'steel',
      }),
      cutting: cutting(1273, 95, 95),
      peckMm: 1.5,
      hole: { x: 0, y: 0, r: 2.5 },
      rMm: -0.5,
      hasR: true,
    },
  ],
})

/** The documents a file of the demo is compared against, by the name of the golden file. */
const PANEL_CASES: { name: string; program: QuickProgram }[] = [
  { name: 'demo', program: demoProgram() },
  { name: 'steel', program: steelProgram() },
]

describe('a demo file is read and written back byte for byte', () => {
  const names = ['demo', 'face', 'pocket', 'circle', 'grid', 'ring', 'hole', 'steel']
  for (const name of names) {
    it(`round-trips ${name}.qprog`, () => {
      const text = etl(`${name}.qprog`)
      const parsed = parseProgram(text)
      if (!parsed.ok) throw new Error(`${name}.qprog did not parse: line ${parsed.line}: ${parsed.reason}`)
      expect(serializeProgram(parsed.program)).toBe(text)
    })
  }
})

describe('the panel document is written in the demo text', () => {
  for (const { name, program } of PANEL_CASES) {
    it(`writes ${name}.qprog as the demo does`, () => {
      expect(serializeProgram(program)).toBe(etl(`${name}.qprog`))
    })
  }
})

describe('the panel round-trip keeps the document', () => {
  for (const { name, program } of PANEL_CASES) {
    it(`reads back the fields of ${name}.qprog`, () => {
      const text = serializeProgram(program)
      const parsed = parseProgram(text)
      if (!parsed.ok) throw new Error(`the panel file of ${name} did not parse: ${parsed.reason}`)
      // The H row equal to the tool number is not written (the demo writer leaves it out), so the
      // read document fills it back from the tool number: the effective G43 row is the same.
      const expected = {
        ...program,
        operations: program.operations.map((op) => ({
          ...op,
          tool: { ...op.tool, hNumber: op.tool.hNumber === op.tool.number ? 0 : op.tool.hNumber },
        })),
      }
      expect(parsed.program).toEqual(expected)
      expect(parsed.program.operations.map(effectiveH)).toEqual(program.operations.map(effectiveH))
    })
  }

  it('writes the same text twice for the same document', () => {
    expect(serializeProgram(demoProgram())).toBe(serializeProgram(demoProgram()))
  })
})

describe('the reader names the line and the field', () => {
  it('refuses an unknown field with its line', () => {
    const text = etl('face.qprog').replace('op.stepOver 60', 'op.stepOver 60\nop.speed 9')
    expect(parseProgram(text)).toEqual({ ok: false, line: 22, reason: expect.stringContaining('op.speed') })
  })

  it('refuses a missing required field with its name', () => {
    const text = etl('face.qprog').replace('op.feed 716\n', '')
    const parsed = parseProgram(text)
    expect(parsed.ok).toBe(false)
    if (parsed.ok) return
    expect(parsed.reason).toContain('op.feed')
  })

  it('refuses a foreign schema', () => {
    const text = etl('face.qprog').replace('schema 1', 'schema 2')
    const parsed = parseProgram(text)
    expect(parsed.ok).toBe(false)
    if (parsed.ok) return
    expect(parsed.line).toBe(1)
    expect(parsed.reason).toContain('2')
  })

  it('refuses a repeated field', () => {
    const text = etl('face.qprog').replace('op.feed 716\n', 'op.feed 716\nop.feed 717\n')
    const parsed = parseProgram(text)
    expect(parsed.ok).toBe(false)
    if (parsed.ok) return
    expect(parsed.reason).toContain('op.feed')
  })

  it('reads a file of the previous build with the defaults', () => {
    // No `g64P`/`g64Q`, no `op.material`, no `op.h`, no `op.stepOver` of a pocket, no `op.drill`.
    const text = [
      'schema 1',
      'name POCKET',
      'units mm',
      'wcs G54',
      'stock -50 -40 -20 50 40 0',
      'safeZ 5',
      'op pocket "Карман"',
      'op.enabled on',
      'op.tool 4 10',
      'op.spindle 4775',
      'op.feed 860',
      'op.plunge 215',
      'op.coolant flood',
      'op.topZ 0',
      'op.depth 3',
      'op.stepDown 1.5',
      'op.rect -25 -15 50 30',
      'op.cornerRadius 0',
      'op.finishAllowance 0.5',
      '',
    ].join('\n')
    const parsed = parseProgram(text)
    if (!parsed.ok) throw new Error(parsed.reason)
    expect(parsed.program.g64P).toBe(0.01)
    expect(parsed.program.g64Q).toBe(0.01)
    expect(parsed.program.stopAfterOp).toBe(true)
    const op = parsed.program.operations[0] as Operation
    expect(op.material).toBe('aluminium')
    expect(op.tool.hNumber).toBe(0)
    if (op.kind !== 'pocket') throw new Error('the pocket did not parse as a pocket')
    expect(op.stepOverPct).toBe(70)
  })
})
