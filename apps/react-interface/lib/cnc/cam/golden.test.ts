// The Q6 golden pair: the demo part of all fifteen kinds and the program the panel writes for it
// (PLAN-QUICKPROG, §9 Q6 and §10 "golden `.ngc`").
//
// The pair in `testdata/quickprog-golden/` is produced by the panel (`golden-doc.ts` through
// `golden-gen.ts`), not by the C++ demo: the point is the file the operator saves. The test reads
// the `.qprog` back with `parseProgram`, plans and emits it again and demands the very same text,
// so a drift of the writer, the reader, the planner or the emitter fails here, not on the machine.
// The viewer rules (`viewerIssues`) and the collision check (`checkProgram`) must pass, the counts
// of every kind are pinned number by number, and the C++ demo reads the panel's document to prove
// the four kinds it plans still agree on the coordinates.
import { execFileSync } from 'node:child_process'
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { afterAll, describe, expect, it } from 'vitest'

import { checkProgram } from './collision'
import { emitProgram } from './emit'
import { GOLDEN_NAME, goldenProgram } from './golden-doc'
import { buildProgram, type ProgramPath } from './path'
import { cutLengthMm } from './path-stats'
import { parseProgram } from './serialize'
import { RIGID_TAPPING_PARAM } from './validate-tap'
import { validate } from './validate'
import { viewerIssues, type Issue } from './viewer-check'
import { countsOf, GOLDEN_COUNTS, written } from './golden-chain'
import type { QuickProgram } from './ops'

const dir = fileURLToPath(new URL('../../../testdata/quickprog-golden/', import.meta.url))
const readText = (name: string): string => readFileSync(`${dir}${name}`, 'utf8')
const linesOf = (text: string): string[] => text.replace(/\n$/, '').split('\n')

/** The demo binary of the previous step, if it was built; the test skips the C++ leg without it. */
const DEMO = process.env.QUICKPROG_BIN ?? '/tmp/opencode/qp-b/quickprog'

/**
 * The document of the file with the machine parameter the panel adds for the tap (`G84`).
 *
 * One read-back detail is normalised here and named out loud: the writer leaves `op.h` out when the
 * H row is the tool number, and the reader opens that row as zero ("the tool number"), so the
 * document of the file and the document of the code differ in `tool.hNumber` only. The `.ngc` is the
 * proof that nothing else drifts: the emitted text of both is the same character for character.
 *
 * A grid drill of the file opens its list instead of a single hole (the document fills `hole` too,
 * the file writes only `op.grid`), so the test asks the file for the field the writer prints: the
 * `pattern`, not the hole under it.
 */
const readGolden = (): QuickProgram => {
  const parsed = parseProgram(readText('demo.qprog'))
  if (!parsed.ok) throw new Error(`demo.qprog: строка ${String(parsed.line)}: ${parsed.reason}`)
  const operations = parsed.program.operations.map((operation) => ({
    ...operation,
    tool: { ...operation.tool, hNumber: operation.tool.hNumber === 0 ? operation.tool.number : operation.tool.hNumber },
  }))
  return { ...parsed.program, operations, params: { [RIGID_TAPPING_PARAM]: 1 } }
}

/** The plan of the golden document; a document that does not plan is a failed test, not an error. */
const planned = (program: QuickProgram): ProgramPath => {
  const built = buildProgram(program)
  if (!built.ok) throw new Error(`the golden document does not plan: ${built.problems.join('; ')}`)
  return built.path
}

/** The one warning of the golden program's viewer check: the park reads as a move to machine zero. */
const VIEWER_WARNINGS = ['op-no-tool', 'park-as-move']

describe('the golden document', () => {
  it('has one enabled operation of each of the fifteen kinds, in the kind order', () => {
    const program = readGolden()
    expect(program.name).toBe(GOLDEN_NAME)
    expect(program.operations).toHaveLength(15)
    expect(program.operations.every((operation) => operation.enabled)).toBe(true)
    expect(program.operations.map((operation) => operation.kind)).toEqual(GOLDEN_COUNTS.map((item) => item.kind))
    // The tools of the table, one row per operation: a number above zero for every cutting kind.
    for (const operation of program.operations) {
      if (operation.kind === 'custom') continue
      expect(operation.tool.number).toBeGreaterThan(0)
      expect(operation.tool.diameterMm).toBeGreaterThan(0)
    }
  })

  it('is the document the panel builds: the `.qprog` is its own writer output', () => {
    const doc = goldenProgram()
    // The file has no machine parameters: they are the machine, not the part. The test compares the
    // fields the writer prints, so a field the file does not carry is not a difference of the pair.
    expect(readGolden().operations.map(written)).toEqual(doc.operations.map(written))
  })
})

describe('the golden program is reproducible', () => {
  it('parses, plans and emits the very text of demo.ngc, character for character', () => {
    const program = readGolden()
    const emitted = emitProgram(program, planned(program)).join('\n')
    expect(emitted).toBe(readText('demo.ngc').replace(/\n$/, ''))
  })

  it('carries the generator line of the panel, not of the demo', () => {
    expect(readText('demo.ngc')).toMatch(/^\( {2}GENERATOR: lynx-touch react-interface \d+\.\d+\.\d+\)$/m)
  })
})

describe('the golden program passes the checks', () => {
  it('validates without a single problem', () => {
    expect(validate(readGolden()).ok).toBe(true)
  })

  it('has no viewer error, and only the two expected warnings', () => {
    const issues: Issue[] = viewerIssues(linesOf(readText('demo.ngc')))
    expect(issues.filter((issue) => issue.level === 'error')).toEqual([])
    // The park is a move to machine zero for the parser, and the custom block names no tool.
    expect(issues.map((issue) => issue.code)).toEqual(VIEWER_WARNINGS)
  })

  it('has no collision error; the three warnings are the drawn ones', () => {
    const program = readGolden()
    const check = checkProgram(program, planned(program))
    expect(check.errors).toEqual([])
    // Three named warnings and nothing else: the seat finer than its head, the thread whose whole
    // turns leave 0.5 mm uncut, and the rest with no source left by the earlier pockets. All are
    // the document's own figures, not a fault (`threadWarnings`, `path-thread.ts`).
    expect(check.warnings).toEqual([
      { field: 'operations[8]', reason: 'Цековка мельче диаметра: проверьте крепёж' },
      {
        field: 'operations[9]',
        reason: 'Резьба короче заданной на 0.5 мм: число витков целое, остаток не режется (6 витков по 1.25 мм).',
      },
      { field: 'operations[12]', reason: 'Остатка нет: предыдущие операции уже сняли площадь' },
    ])
  })
})

describe('the golden program has known numbers', () => {
  const program = readGolden()
  const path = planned(program)

  it('plans 289 moves and 2949.0 mm of cut', () => {
    expect(path.moves.length).toBe(289)
    expect(Number(cutLengthMm(path.moves).toFixed(1))).toBe(2949)
  })

  it('emits 485 lines', () => {
    expect(linesOf(readText('demo.ngc'))).toHaveLength(485)
  })

  it('gives every kind the moves and the cut length of the golden table', () => {
    expect(countsOf(program, path)).toEqual(GOLDEN_COUNTS)
  })

  it('never cuts below the bottom of its own pass, kind by kind', () => {
    // The bottom a cut may reach: the depth of the frame, or the milled thread's own length, which
    // is the whole of the pass (its pitch decides how far the helix actually goes).
    const bottomOf = (operation: QuickProgram['operations'][number]): number =>
      operation.topZ - (operation.kind === 'thread' ? operation.threadDepthMm : operation.depthMm)
    const spans = path.operations
    program.operations.forEach((operation, opIndex) => {
      const span = spans.find((item) => item.opIndex === opIndex)
      if (span === undefined) return
      const next = spans.find((item) => item.first > span.first)?.first ?? path.moves.length
      const bottom = bottomOf(operation)
      for (const move of path.moves.slice(span.first, next)) {
        if (move.kind !== 'cut') continue
        expect(move.to.z, `${operation.kind}: Z ниже дна прохода`).toBeGreaterThanOrEqual(bottom - 1e-9)
      }
    })
  })

  it('leaves the three hand-check kinds empty on purpose', () => {
    for (const item of GOLDEN_COUNTS.filter((count) => ['rest', 'custom', 'probe'].includes(count.kind))) {
      expect(item).toEqual({ ...item, moves: 0, cuts: 0, lengthMm: 0 })
    }
    // The custom block is not silent: its own lines are in the file, after the tags.
    expect(linesOf(readText('demo.ngc'))).toContain('N2105 G1 Z5 F100')
  })
})

/**
 * The positions of the holes a drilling cycle names, in the order of the file: a `G8x` block and the
 * following blocks that carry only X/Y are the cycle's holes. The demo and the panel write the same
 * coordinates for the grid, so a shifted hole fails here.
 */
const cycleHoles = (lines: string[]): string[] => {
  const holes: string[] = []
  for (const line of lines) {
    const code = line.replace(/^N\d+ /, '').replace(/ F[\d.]+/g, '')
    if (/^G8[0-9] X/.test(code) || (/^X-?[\d.]+ Y-?[\d.]+$/.test(code) && holes.length > 0)) {
      holes.push(code.replace(/^G8[0-9] /, '').replace(/ Z-?[\d.]+ R-?[\d.]+ Q-?[\d.]+/, ''))
    }
  }
  return holes
}

describe('the C++ demo reads the panel document', () => {
  const work = existsSync(DEMO) ? mkdtempSync(join(tmpdir(), 'qp-golden-')) : ''
  afterAll(() => {
    if (work.length > 0) rmSync(work, { recursive: true, force: true })
  })

  it.skipIf(!existsSync(DEMO))('skips the eleven kinds the demo does not plan and names each one', () => {
    const out = join(work, 'demo.ngc')
    const text = execFileSync(DEMO, ['--qprog', `${dir}demo.qprog`, out], { encoding: 'utf8' })
    const skipped = text.split('\n').filter((line) => line.includes('не поддерживается демо'))
    expect(skipped).toHaveLength(11)
    // The four kinds the demo plans: face, pocket, circle pocket and drill, one line each.
    expect(text).toMatch(/Итого: операций 4,/)
  })

  it.skipIf(!existsSync(DEMO))('agrees with the panel on the motion kind of the four common kinds', () => {
    const out = join(work, 'demo.ngc')
    execFileSync(DEMO, ['--qprog', `${dir}demo.qprog`, out], { encoding: 'utf8' })
    const demo = readFileSync(out, 'utf8')
    const panelLines = linesOf(readText('demo.ngc'))
    const demoLines = linesOf(demo)
    // The sequence of motion codes per operation: `G0`/`G1` strips, the `G3` helix of the circle,
    // and the rapids of the drill. A difference of a strip, an arc or a cycle fails here.
    const motions = (lines: string[], op: string): string[] => {
      const span = lines.slice(lines.indexOf(`(UI_OP: ${op})`))
      const end = span.findIndex((line) => /^N\d+ M1$/.test(line))
      return span
        .slice(0, end < 0 ? undefined : end)
        .map((line) => /^N\d+ (G[0-3])\b/.exec(line))
        .filter((match): match is RegExpExecArray => match !== null)
        .map((match) => match[1])
    }
    for (const op of ['Торцевание', 'Карман', 'Круглый карман', 'Сверление']) {
      expect(motions(demoLines, op)).toEqual(motions(panelLines, op))
    }
    // The holes of `G83`: the same six centres, the same order (the drill is the only cycle of the
    // four kinds the demo plans; the panel has more cycles of the later kinds).
    expect(cycleHoles(demoLines)).toEqual(cycleHoles(panelLines).slice(0, cycleHoles(demoLines).length))
  })
})
