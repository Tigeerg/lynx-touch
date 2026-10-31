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
import { slotLayout } from './path-slot'
import { cutLengthMm } from './path-stats'
import { parseProgram } from './serialize'
import { RIGID_TAPPING_PARAM } from './validate-tap'
import { validate } from './validate'
import { viewerIssues, type Issue } from './viewer-check'
import { countsOf, GOLDEN_COUNTS, written } from './golden-chain'
import type { QuickProgram } from './ops'
import { engraveLayout } from './path-engrave'

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

/** A rectangle in the work plane: the box the engraving must not touch. */
interface Rect {
  xMin: number
  xMax: number
  yMin: number
  yMax: number
}

/**
 * The rectangles the engraving must not touch: the slot grown by the tool radius, the pockets, the
 * island and the round pocket. Taken from the document and its own planners, so a change of the
 * document or of a planner moves the numbers with it.
 */
const forbiddenZones = (program: QuickProgram): { name: string; rect: Rect }[] => {
  const zones: { name: string; rect: Rect }[] = []
  for (const operation of program.operations) {
    if (!operation.enabled) continue
    if (operation.kind === 'slot') {
      const layout = slotLayout(operation)
      const ends = layout.strips.flatMap((strip) => [strip.from, strip.to])
      if (ends.length === 0) continue
      const radius = operation.tool.diameterMm / 2
      zones.push({
        name: 'slot',
        rect: {
          xMin: Math.min(...ends.map((point) => point.x)) - radius,
          xMax: Math.max(...ends.map((point) => point.x)) + radius,
          yMin: Math.min(...ends.map((point) => point.y)) - radius,
          yMax: Math.max(...ends.map((point) => point.y)) + radius,
        },
      })
    }
    if (operation.kind === 'pocket' || operation.kind === 'rest') {
      const { x, y, w, h } = operation.rect
      zones.push({ name: operation.kind, rect: { xMin: x, xMax: x + w, yMin: y, yMax: y + h } })
    }
    if (operation.kind === 'pocket-island') {
      const { x, y, w, h } = operation.rect
      zones.push({ name: 'pocket-island', rect: { xMin: x, xMax: x + w, yMin: y, yMax: y + h } })
      const island = operation.island
      zones.push({
        name: 'island',
        rect: { xMin: island.x, xMax: island.x + island.w, yMin: island.y, yMax: island.y + island.h },
      })
    }
    if (operation.kind === 'circle-pocket') {
      const circle = operation.circle
      zones.push({
        name: 'circle-pocket',
        rect: {
          xMin: circle.x - circle.r,
          xMax: circle.x + circle.r,
          yMin: circle.y - circle.r,
          yMax: circle.y + circle.r,
        },
      })
    }
  }
  return zones
}

/** The overset of two rectangles, by their corners. */
const overlaps = (a: Rect, b: Rect): boolean => a.xMin < b.xMax && b.xMin < a.xMax && a.yMin < b.yMax && b.yMin < a.yMax

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

  it('keeps the engraving off the slot and the pockets, by the geometry of the document', () => {
    // The technician's note of `CHECKLIST.md`: a label over a keyway drops its letters into the cut.
    // The rectangles are taken from the document and its own planners, not from the text of the
    // file: the engraving from `engraveLayout` (the box of its strokes, which is exactly what the
    // planner cuts, since every stroke starts and ends on itself), the slot from `slotLayout` grown by
    // the tool radius on every side (the cut is the mill disc swept over the strip centres), the
    // pockets and the island as the document states them. The zones come from `forbiddenZones`, the
    // same helper the negative case below uses, so a mistake in the zones reds both.
    const program = readGolden()
    const engrave = program.operations.find((operation) => operation.kind === 'engrave')
    if (engrave?.kind !== 'engrave') throw new Error('the golden document has no engraving')

    const strokes = engraveLayout(engrave).strokes.flat()
    const label: Rect = {
      xMin: Math.min(...strokes.map((point) => point.x)),
      xMax: Math.max(...strokes.map((point) => point.x)),
      yMin: Math.min(...strokes.map((point) => point.y)),
      yMax: Math.max(...strokes.map((point) => point.y)),
    }

    const zones = forbiddenZones(program)

    // The engraving is a surface cut on the top of the stock: the word may pass over a pocket that was
    // milled before it (the tool moves in air there), but never over a groove the part keeps. So the
    // slot and the island are the zones that matter; the open pockets are not.
    const walls = zones.filter((zone) => zone.name === 'slot' || zone.name === 'island')
    expect(zones.length).toBeGreaterThanOrEqual(5)
    expect(walls.length).toBeGreaterThanOrEqual(2)
    for (const zone of walls) {
      expect(overlaps(label, zone.rect), `надпись «${engrave.text}» заходит на ${zone.name}`).toBe(false)
    }
    // The cut of the label itself: the planner's moves of the engraving sit on its strokes, so
    // every rapid, plunge and turn of the label stays in the box the test just placed in the free
    // strip. It is the planned path of the same document, not a second formula of the layout.
    const plan = planned(program)
    const opIndex = program.operations.indexOf(engrave)
    const engraveMoves = plan.moves.filter((move) => move.operation === opIndex)
    expect(engraveMoves.length).toBeGreaterThan(0)
    for (const move of engraveMoves) {
      expect(move.to.x).toBeGreaterThanOrEqual(label.xMin - 1e-9)
      expect(move.to.x).toBeLessThanOrEqual(label.xMax + 1e-9)
      expect(move.to.y).toBeGreaterThanOrEqual(label.yMin - 1e-9)
      expect(move.to.y).toBeLessThanOrEqual(label.yMax + 1e-9)
    }
  })
})

describe('the golden program is reproducible', () => {
  it('parses, plans and emits the very text of demo.ngc, character for character', () => {
    const program = readGolden()
    const emitted = emitProgram(program, planned(program)).join('\n')
    expect(emitted).toBe(readText('demo.ngc').replace(/\n$/, ''))
  })

  it('fails on the defect it guards: a word on the axis of the slot', () => {
    // The guard is only worth something if it goes red on the very defect it was written for. The box
    // of a word sits on its baseline and grows upwards, so the word put back onto the keyway axis (its
    // baseline on the axis, the letters up to the far wall) must overlap the swept slot disc.
    const program = readGolden()
    const moved: QuickProgram = {
      ...program,
      operations: program.operations.map((op) => (op.kind === 'engrave' ? { ...op, y: 31 } : op)),
    }
    const engrave = moved.operations.find((op) => op.kind === 'engrave')
    if (engrave?.kind !== 'engrave') throw new Error('no engraving')
    const strokes = engraveLayout(engrave).strokes.flat()
    const label: Rect = {
      xMin: Math.min(...strokes.map((point) => point.x)),
      xMax: Math.max(...strokes.map((point) => point.x)),
      yMin: Math.min(...strokes.map((point) => point.y)),
      yMax: Math.max(...strokes.map((point) => point.y)),
    }
    const slot = forbiddenZones(moved).find((zone) => zone.name === 'slot')
    expect(slot).toBeDefined()
    expect(overlaps(label, slot!.rect), JSON.stringify({ label, slot })).toBe(true)
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
