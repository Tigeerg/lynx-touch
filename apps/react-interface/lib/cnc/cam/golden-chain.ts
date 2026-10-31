// The helpers of `golden.test.ts`: the counts of one planned operation and the fields of a kind the
// `.qprog` carries (PLAN-QUICKPROG, §9 Q6).
//
// Kept apart so the test file stays at the size guide, and so the golden table (the pinned numbers of
// every kind) is one readable block, not a list between the cases.
import { cutLengthMm } from './path-stats'
import type { ProgramPath } from './path'
import type { Operation, QuickProgram } from './ops'

/**
 * A span of the path by its place in the list of planned spans (not by the document index): a kind
 * that plans no move (a rest with nothing left, a custom block) has no span, and the spans after it
 * shift. The golden document has one operation per kind, so a span's place is the kind's place less
 * the kinds that planned nothing before it.
 */
const spanOf = (path: ProgramPath, planIndex: number) => {
  const span = path.operations[planIndex]
  const next = planIndex + 1 < path.operations.length ? path.operations[planIndex + 1].first : path.moves.length
  return { moves: path.moves.slice(span.first, next), first: span.first }
}

/** A kind, its move count, its cut count and the length of its cut moves (mm, one decimal). */
export interface KindCount {
  kind: Operation['kind']
  moves: number
  cuts: number
  lengthMm: number
}

export const countsOf = (program: QuickProgram, path: ProgramPath): KindCount[] => {
  const byDocument = new Map<number, KindCount>()
  path.operations.forEach((span, planIndex) => {
    const { moves, first } = spanOf(path, planIndex)
    byDocument.set(span.opIndex, {
      kind: program.operations[span.opIndex].kind,
      moves: moves.length,
      cuts: moves.filter((move) => move.kind === 'cut').length,
      lengthMm: Number(cutLengthMm(moves, first).toFixed(1)),
    })
  })
  return program.operations.map(
    (operation, opIndex) => byDocument.get(opIndex) ?? { kind: operation.kind, moves: 0, cuts: 0, lengthMm: 0 },
  )
}

/** The fields each kind writes to the `.qprog`; the other fields of a read-back are the defaults. */
const WRITTEN: Record<Operation['kind'], string[]> = {
  face: ['z', 'stepOverPct'],
  pocket: ['rect', 'cornerRadiusMm', 'finishAllowanceMm', 'stepOverPct'],
  'circle-pocket': ['circle', 'finishAllowanceMm', 'stepOverPct'],
  drill: ['pattern', 'x0', 'y0', 'cols', 'rows', 'dx', 'dy', 'peckMm', 'body'],
  contour: ['points', 'closed', 'allowanceMm', 'side'],
  slot: ['from', 'to', 'widthMm', 'stepOverPct'],
  chamfer: ['from', 'to', 'chamferMm'],
  tap: ['pattern', 'pitchMm', 'hole'],
  counterbore: ['pattern', 'hole', 'seatDiameterMm', 'seatDepthMm', 'dwellSec', 'rMm', 'hasR'],
  thread: ['hole', 'pitchMm', 'internal', 'threadDepthMm', 'threadAllowanceMm', 'stepOverPct'],
  engrave: ['text', 'x', 'y', 'heightMm', 'angleDeg'],
  'pocket-island': ['rect', 'island', 'islandAllowanceMm', 'stepOverPct'],
  rest: ['source', 'rect', 'allowanceMm', 'stepOverPct'],
  custom: ['gcode'],
  probe: ['points', 'rangeMm', 'touchFeedMmMin', 'axis'],
}

/** The fields of one operation the file carries: the common ones and the kind's own. */
export const written = (operation: Operation): Record<string, unknown> => {
  const common = ['id', 'kind', 'enabled', 'tool', 'cutting', 'coolant', 'material', 'topZ', 'depthMm', 'stepDownMm']
  const out: Record<string, unknown> = {}
  for (const key of [...common, ...WRITTEN[operation.kind]]) {
    if (key in operation) out[key] = (operation as unknown as Record<string, unknown>)[key]
  }
  return out
}

export const GOLDEN_COUNTS: KindCount[] = [
  { kind: 'face', moves: 56, cuts: 33, lengthMm: 1170.4 },
  { kind: 'pocket', moves: 34, cuts: 30, lengthMm: 810 },
  { kind: 'circle-pocket', moves: 18, cuts: 14, lengthMm: 76 },
  { kind: 'drill', moves: 6, cuts: 0, lengthMm: 0 },
  { kind: 'contour', moves: 8, cuts: 6, lengthMm: 222 },
  { kind: 'slot', moves: 10, cuts: 6, lengthMm: 132 },
  { kind: 'chamfer', moves: 7, cuts: 5, lengthMm: 66 },
  { kind: 'tap', moves: 1, cuts: 0, lengthMm: 0 },
  { kind: 'counterbore', moves: 1, cuts: 0, lengthMm: 0 },
  { kind: 'thread', moves: 32, cuts: 28, lengthMm: 118.6 },
  { kind: 'engrave', moves: 71, cuts: 47, lengthMm: 90.1 },
  { kind: 'pocket-island', moves: 45, cuts: 31, lengthMm: 263.8 },
  // No source left behind the earlier pocket: the rest plans nothing and the check says so.
  { kind: 'rest', moves: 0, cuts: 0, lengthMm: 0 },
  { kind: 'custom', moves: 0, cuts: 0, lengthMm: 0 },
  { kind: 'probe', moves: 0, cuts: 0, lengthMm: 0 },
]
