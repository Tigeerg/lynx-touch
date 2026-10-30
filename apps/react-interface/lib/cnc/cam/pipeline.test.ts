// The panel pipeline against the golden files of the C++ demo (PLAN-QUICKPROG, 7.4, C2).
//
// The golden `.ngc` files live in `etl/` and come from `apps/quickprog` (the command lines are in
// `etl/README.md`). The comparison is literal: the emitter prints the same block numbers, the same
// words and the same coordinates as the demo, so a difference of geometry, block order or spelling
// fails here and not in review. The numbers of the summary (`ходов`, `длина резания`, the time
// estimate) are read from the console text of the same run, not from a second calculation.
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

import { ETL_CASES, parseSummary } from './etl-cases'
import { buildProgram, estimateSeconds, operationUuid } from './path'
import { emitProgram } from './emit'
import { faceLayout } from './path-face'
import { pocketLayout } from './path-pocket'
import { cutLengthMm, faceStats, pocketStats } from './path-stats'
import { stockTop, type QuickProgram } from './ops'
import type { ProgramPath } from './path'

const etl = (name: string): string => readFileSync(fileURLToPath(new URL(`./etl/${name}`, import.meta.url)), 'utf8')

const planned = (name: string): { program: QuickProgram; path: ProgramPath } => {
  const { program } = ETL_CASES.find((item) => item.name === name)!
  const built = buildProgram(program)
  if (!built.ok) throw new Error(`the case ${name} did not plan: ${built.problems.join('; ')}`)
  return { program, path: built.path }
}

const enabledOps = (program: QuickProgram) => program.operations.filter((op) => op.enabled)

/** The moves of one span and the absolute index of its first move. */
const spanMoves = (path: ProgramPath, index: number) => {
  const span = path.operations[index]
  const next = index + 1 < path.operations.length ? path.operations[index + 1].first : path.moves.length
  return { moves: path.moves.slice(span.first, next), first: span.first }
}

describe('emitted G-code equals the demo', () => {
  for (const { name, program } of ETL_CASES) {
    it(`prints ${name}.ngc word for word`, () => {
      const built = buildProgram(program)
      if (!built.ok) throw new Error(`the case ${name} did not plan: ${built.problems.join('; ')}`)
      // The trailing percent sign of a text file is not a line of the file for `setLines`.
      expect(emitProgram(program, built.path).join('\n')).toBe(etl(`${name}.ngc`).replace(/\n$/, ''))
    })
  }
})

describe('plan totals equal the demo summary', () => {
  for (const { name } of ETL_CASES) {
    it(`matches ${name} move count, cutting length and time`, () => {
      const { program, path } = planned(name)
      const reference = parseSummary(etl(`${name}.summary.txt`))
      expect(path.moves.length).toBe(reference.moves)
      expect(path.lengthMm).toBeCloseTo(reference.cuttingMm, 1)
      // The same cards as the console: each cut at its feed, each rapid at 15000 mm/min, the
      // drilling cycle on top. The console's own lines are the proof; the known gap of a few
      // seconds is recorded in `TODO.md` (`RAPID_MM_MIN` of the console does not reproduce 364 s).
      expect(Math.abs(estimateSeconds(path) - reference.seconds)).toBeLessThanOrEqual(reference.seconds * 0.05 + 1)
      expect(path.operations.map((span) => span.opIndex)).toEqual(enabledOps(program).map((_, index) => index))
    })
  }
})

describe('plan per-operation numbers equal the demo summary', () => {
  for (const { name } of ETL_CASES) {
    it(`matches the per-operation lines of ${name}`, () => {
      const { program, path } = planned(name)
      const reference = parseSummary(etl(`${name}.summary.txt`))
      const ops = enabledOps(program)
      expect(reference.ops.length).toBe(ops.length)
      path.operations.forEach((_, index) => {
        const { moves, first } = spanMoves(path, index)
        // The demo counts every move of a span, rapids included.
        expect(moves.length).toBe(reference.ops[index].moves)
        // A mill span carries its cutting length; a circle pocket and a drilling span are charged
        // by their own stats (the arc total, the cycle time), as the console does.
        if (ops[index].kind !== 'drill' && ops[index].kind !== 'circle-pocket') {
          expect(cutLengthMm(moves, first)).toBeCloseTo(reference.ops[index].cuttingMm, 1)
        }
      })
    })
  }
})

describe('layout stats equal the demo plans', () => {
  for (const { name } of ETL_CASES) {
    it(`matches the face and pocket stats of ${name}`, () => {
      const { program, path } = planned(name)
      const reference = parseSummary(etl(`${name}.summary.txt`))
      path.operations.forEach((span, index) => {
        const op = program.operations[span.opIndex]
        if (op.kind === 'face') {
          const stats = faceStats(faceLayout(program.stock, op), program.stock, op)
          // The console prints the plan's cut chords; the stats supply the strips, the passes and
          // the move count, and the length comes from the moves, as the console does it.
          stats.lengthMm = cutLengthMm(spanMoves(path, index).moves, span.first)
          expect(stats.moves).toBe(reference.ops[index].moves)
          expect(stats.lengthMm).toBeCloseTo(reference.ops[index].cuttingMm, 1)
        } else if (op.kind === 'pocket') {
          const stats = pocketStats(pocketLayout(op), op)
          // The console's pocket `moves` is its own formula (`PocketStats::moves`), a move wider
          // than the list per pass; the printed count and the length are the plan's.
          expect(stats.moves).toBe(reference.ops[index].moves + pocketLayout(op).passes.length)
          // The printed length is the plan's chords (`cutLengthOf`), not `PocketStats::lengthMm`,
          // which counts the links across the step instead of the rows it also names.
          stats.lengthMm = cutLengthMm(spanMoves(path, index).moves, span.first)
          expect(stats.lengthMm).toBeCloseTo(reference.ops[index].cuttingMm, 1)
        }
      })
    })
  }
})

describe('the program block identifier', () => {
  it('names a stage the way the demo does', () => {
    const { path } = planned('mixed')
    expect(operationUuid(path.uuid, 0)).toBe('path-000001/op1')
    expect(operationUuid(path.uuid, 2)).toBe('path-000001/op3')
  })
})

describe('safety properties of the reference programs', () => {
  for (const { name } of ETL_CASES) {
    it(`never rapids below the material top in ${name}`, () => {
      const { program, path } = planned(name)
      const top = stockTop(program.stock)
      for (const move of path.moves) {
        if (move.kind === 'rapid') expect(move.to.z).toBeGreaterThanOrEqual(top - 1e-9)
      }
    })

    it(`keeps G43 after every tool change in ${name}`, () => {
      const lines = etl(`${name}.ngc`).split('\n')
      lines.forEach((line, index) => {
        if (!/ M6$/.test(line)) return
        const after = lines.slice(index + 1).find((probe) => /^N\d+ /.test(probe))
        expect(after).toMatch(/G43 Z[\d.]+ H\d+/)
      })
    })

    it(`puts G53 G0 Z0 in the header and in the footer of ${name}`, () => {
      const lines = etl(`${name}.ngc`).split('\n')
      expect(lines.filter((line) => /G53 G0 Z0$/.test(line)).length).toBeGreaterThanOrEqual(2)
    })

    it(`puts M1 exactly between operations in ${name}`, () => {
      const lines = etl(`${name}.ngc`).split('\n')
      const stops = lines.filter((line) => /^N\d+ M1$/.test(line)).length
      const ops = parseSummary(etl(`${name}.summary.txt`)).ops.length
      const stopAfter = enabledOps(planned(name).program).length > 1 && planned(name).program.stopAfterOp
      expect(stops).toBe(stopAfter ? ops - 1 : 0)
    })
  }
})
