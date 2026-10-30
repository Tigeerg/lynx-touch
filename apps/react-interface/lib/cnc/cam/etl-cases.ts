// The panels and the emitter have to match the output of the C++ demo (PLAN-QUICKPROG, 7.4, C2).
//
// The reference files in `etl/` are produced by `apps/quickprog` for the same parameters (the
// command lines are in `etl/README.md`), so a difference in geometry, in the block order or in a
// word is a failed test, not a matter of review. The documents here are the ones `defaults.ts` and
// `ops.ts` build, with the fields the demo command line sets.
import type { Operation, QuickProgram } from './ops'
import { newOperation, newProgram } from './defaults'

/** The demo stock of every reference case except the mixed one. */
const CONSOLE_STOCK = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

const tool = (number: number, diameterMm: number, hNumber = number) => ({ number, diameterMm, hNumber })

const cutting = (spindleRpm: number, feedMmMin: number, plungeFeedMmMin: number) => ({
  spindleRpm,
  feedMmMin,
  plungeFeedMmMin,
})

export interface EtlCase {
  /** The name of the `.ngc`/`.summary.txt` pair in `etl/`. */
  name: string
  program: QuickProgram
}

/** `--op face --tool 1 --diameter 12 --depth 5 --step-down 2 --step-over 60`. */
const faceCase = (): QuickProgram => {
  const op: Operation = {
    ...newOperation('face', { id: 'Плоскость', topZ: 0, depthMm: 5, stepDownMm: 2, tool: tool(1, 12) }),
    cutting: cutting(3979, 716, 179),
    z: -5,
    stepOverPct: 60,
  }
  return { ...newProgram(CONSOLE_STOCK, 'FACE'), operations: [op] }
}

/** `--op pocket --tool 3 --diameter 8 --x -30 --y -20 --size 60x40 --step-over 70`, the demo pair. */
const pocketCase = (): QuickProgram => {
  const op: Operation = {
    ...newOperation('pocket', { id: 'Карман', topZ: 0, depthMm: 6, stepDownMm: 2.5, tool: tool(3, 8) }),
    cutting: cutting(5968, 1074, 269),
    rect: { x: -30, y: -20, w: 60, h: 40 },
  }
  return { ...newProgram(CONSOLE_STOCK, 'POCKET'), operations: [op] }
}

/** `--op circle-pocket --tool 2 --diameter 8 --pocket-dia 30 --x 10 --y 5 --allowance 0.3`. */
const circleCase = (): QuickProgram => {
  const op: Operation = {
    ...newOperation('circle-pocket', {
      id: 'Круглый карман',
      topZ: 0,
      depthMm: 4,
      stepDownMm: 1.5,
      tool: tool(2, 8),
    }),
    cutting: cutting(5968, 1074, 269),
    circle: { x: 10, y: 5, r: 15 },
    finishAllowanceMm: 0.3,
    stepOverPct: 50,
  }
  return { ...newProgram(CONSOLE_STOCK, 'CIRCLE'), operations: [op] }
}

/** `--op drill --tool 4 --diameter 6 --pattern grid --x -20 --y -10 --cols 3 --rows 2 --dx 20 --dy 15`. */
const gridCase = (): QuickProgram => {
  const op: Operation = {
    ...newOperation('drill', { id: 'Отверстия', topZ: 0, depthMm: 12, tool: tool(4, 6) }),
    cutting: cutting(2122, 255, 255),
    pattern: 'grid',
    x0: -20,
    y0: -10,
    cols: 3,
    rows: 2,
    dx: 20,
    dy: 15,
    hole: { x: -20, y: -10, r: 3 },
  }
  return { ...newProgram(CONSOLE_STOCK, 'DRILL'), operations: [op] }
}

/** `--op drill --tool 7 --diameter 5 --pattern circle --count 4 --radius 20 --start 45`, carbide. */
const ringCase = (): QuickProgram => {
  const op: Operation = {
    ...newOperation('drill', { id: 'Отверстия', topZ: 0, depthMm: 5, tool: tool(7, 5) }),
    cutting: cutting(7639, 1146, 1146),
    body: 'carbide',
    pattern: 'circle',
    hole: { x: 5, y: 5, r: 20 },
    count: 4,
    startDeg: 45,
  }
  return { ...newProgram(CONSOLE_STOCK, 'DRILL'), operations: [op] }
}

/** `--op drill --tool 3 --diameter 5 --x -10 --y 5 --depth 3`: one plain hole, G81. */
const holeCase = (): QuickProgram => {
  const op: Operation = {
    ...newOperation('drill', { id: 'Отверстия', topZ: 0, depthMm: 3, tool: tool(3, 5) }),
    cutting: cutting(2546, 255, 255),
    hole: { x: -10, y: 5, r: 2.5 },
  }
  return { ...newProgram(CONSOLE_STOCK, 'DRILL'), operations: [op] }
}

/**
 * The document of `mixed.qprog`: three operations in G55, a 0.02/0.01 G64, a mist coolant on the
 * pocket and a G83 peck of 2 mm on the drilling.
 */
const mixedCase = (): QuickProgram => {
  const face: Operation = {
    ...newOperation('face', {
      id: 'Черновая плоскость',
      topZ: 0,
      depthMm: 3,
      stepDownMm: 2,
      tool: tool(1, 12),
    }),
    cutting: cutting(3979, 716, 179),
    z: -3,
  }
  const pocket: Operation = {
    ...newOperation('pocket', { id: 'Карман', topZ: 0, depthMm: 5, stepDownMm: 2.5, tool: tool(2, 8) }),
    cutting: cutting(5968, 859, 215),
    coolant: 'mist',
    rect: { x: -20, y: -12, w: 40, h: 24 },
    finishAllowanceMm: 0.4,
    stepOverPct: 50,
  }
  const grid: Operation = {
    ...newOperation('drill', { id: 'Сетка', topZ: 0, depthMm: 8, tool: tool(3, 5), peckMm: 2 }),
    cutting: cutting(6366, 637, 637),
    pattern: 'grid',
    x0: -20,
    y0: -10,
    cols: 2,
    rows: 2,
    dx: 40,
    dy: 20,
    hole: { x: -20, y: -10, r: 2.5 },
  }
  return {
    ...newProgram({ min: { x: -40, y: -30, z: -15 }, max: { x: 40, y: 30, z: 0 } }, 'MIXED'),
    wcs: 'G55',
    safeZ: 8,
    g64P: 0.02,
    g64Q: 0.01,
    operations: [face, pocket, grid],
  }
}

/** `sametool.qprog`: one tool twice, another H row, no M8/M9, the third operation disabled. */
const sametoolCase = (): QuickProgram => {
  const face: Operation = {
    ...newOperation('face', { id: 'A', topZ: 0, depthMm: 1, stepDownMm: 1, tool: tool(2, 10, 5) }),
    cutting: cutting(4775, 860, 215),
    coolant: 'off',
    z: -1,
  }
  const pocket: Operation = {
    ...newOperation('pocket', { id: 'B', topZ: 0, depthMm: 2, stepDownMm: 2, tool: tool(2, 10) }),
    cutting: cutting(6000, 900, 225),
    coolant: 'off',
    rect: { x: -10, y: -10, w: 20, h: 20 },
  }
  const off: Operation = {
    ...newOperation('face', { id: 'C off', topZ: 0, depthMm: 1, stepDownMm: 1, tool: tool(3, 6) }),
    cutting: cutting(7000, 1000, 250),
    coolant: 'off',
    enabled: false,
    z: -1,
  }
  return {
    ...newProgram({ min: { x: -30, y: -30, z: -10 }, max: { x: 30, y: 30, z: 0 } }, 'SAMETOOL'),
    stopAfterOp: true,
    operations: [face, pocket, off],
  }
}

/** Every reference case, in file order. */
export const ETL_CASES: EtlCase[] = [
  { name: 'face', program: faceCase() },
  { name: 'pocket', program: pocketCase() },
  { name: 'circle', program: circleCase() },
  { name: 'grid', program: gridCase() },
  { name: 'ring', program: ringCase() },
  { name: 'hole', program: holeCase() },
  { name: 'mixed', program: mixedCase() },
  { name: 'sametool', program: sametoolCase() },
]

/** The numbers of one console summary of the demo: the totals and the per-operation lines. */
export interface SummaryRef {
  /** The move count and the cutting length of the whole program, from the `Итого` line. */
  moves: number
  cuttingMm: number
  /** The time estimate of the console, seconds. */
  seconds: number
  /** Per enabled operation: the move count, the cutting length and (drilling) the hole count. */
  ops: { moves: number; cuttingMm: number; holes: number | null }[]
}

const number1 = (text: string, pattern: RegExp): number => {
  const match = pattern.exec(text)
  if (match === null) throw new Error(`the demo summary has no ${String(pattern)} line`)
  return Number(match[1])
}

/**
 * Parses the console summary of a reference case. The numbers come from the demo's own text, so the
 * test compares the panel against the demo and not against a second copy of the panel arithmetic.
 */
export const parseSummary = (text: string): SummaryRef => {
  const total = /Итого: операций \d+, ходов (\d+), длина резания ([\d.]+) мм, оценка \d+ мин \d+ с/.exec(text)
  if (total === null) throw new Error('the demo summary has no "Итого" line')
  const ops: SummaryRef['ops'] = []
  let current: SummaryRef['ops'][number] | null = null
  for (const line of text.split('\n')) {
    if (line.startsWith('Итого:')) break
    if (line.startsWith('Операция ')) {
      current = { moves: 0, cuttingMm: 0, holes: null }
      ops.push(current)
      continue
    }
    if (current === null) continue
    const holes = /^ {2}отверстий: (\d+)/.exec(line)
    if (holes !== null) {
      current.holes = Number(holes[1])
      continue
    }
    const moves = /ходов: (\d+)/.exec(line)
    if (moves !== null) current.moves = Number(moves[1])
    // The length line is separate for a mill and joined with the move count for a pocket.
    const length = /длина резания: ([\d.]+) мм/.exec(line)
    if (length !== null) current.cuttingMm = Number(length[1])
  }
  return {
    moves: Number(total[1]),
    cuttingMm: Number(total[2]),
    seconds: number1(text, /Оценка времени: (\d+) с/),
    ops,
  }
}
