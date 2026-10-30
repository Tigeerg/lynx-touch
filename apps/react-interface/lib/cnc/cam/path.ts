// Program assembly: operations -> one path (PLAN-QUICKPROG, 7.4), the port of `path.cpp`.
//
// The dispatch is by `kind`: every kind has a planner and the switch returns its result, so a new
// kind does not compile until it is planned and cannot be silently dropped, the way the demo's
// std::variant visitor refuses to build. A disabled operation is skipped and gets no span.
//
// The spans delimit the moves of each operation in the shared list: the emitter prints UI_OP at the
// start of a span, the preview colours an operation by it. An operation without moves still gets a
// span, so the emitter prints its tag.
//
// The numbers are the cutting length (cut moves only, an arc by its length), the drilling cycle time
// and the time estimate (each cut at its feed, each rapid at the machine rapid rate). The plan is
// validated first, so a document with an error is never planned.
import type { CamMove, CamPath } from './moves'
import type { Operation, QuickProgram } from './ops'
import { moveLength, PathBuilder } from './path-builder'
import { planFace } from './path-face'
import { planRectPocket } from './path-pocket'
import { planCirclePocket } from './path-circle-pocket'
import { planContour } from './path-contour'
import { planSlot } from './path-slot'
import { planChamfer } from './path-chamfer'
import { drillCycleMinutes, planDrill } from './path-drill'
import { planTap, tapCycleMinutes } from './path-tap'
import { planCounterbore } from './path-counterbore'
import { planThread } from './path-thread'
import { counterboreMinutes } from './bores'
import { validate } from './validate'

export { moveLength } from './path-builder'

/** Which moves belong to which operation: `first` is the index of its first move in the path. */
export interface OperationSpan {
  opIndex: number
  first: number
}

/** A path with its spans: the panel keeps the order of the operations the emitter prints. */
export interface ProgramPath extends CamPath {
  operations: OperationSpan[]
}

/** The plan of a program, or the reasons it cannot be planned. */
export type PlanResult = { ok: true; path: ProgramPath } | { ok: false; problems: string[] }

/** The rapid rate of the time estimate, mm/min: 250 mm/s of the joint table, as the demo charges. */
export const RAPID_MM_MIN = 15000

/** The distance of a move from `from`: an arc by its length, a line by its chord in space. */
const travelMm = (move: CamMove, from: CamMove['to']): number =>
  moveLength(move, from) || Math.hypot(move.to.x - from.x, move.to.y - from.y, move.to.z - from.z)

/**
 * The time estimate of a path, seconds, plus the drilling cycle time, minutes. The cards are the
 * console's: each cut at its feed, each rapid at `RAPID_MM_MIN`, the cycle time on top.
 */
export function estimateSeconds(path: CamPath): number {
  let seconds = 0
  let from: CamMove['to'] | null = null
  for (const move of path.moves) {
    if (from !== null) {
      const rate = move.kind === 'cut' ? (move.feedMmMin ?? 0) : RAPID_MM_MIN
      if (rate > 0) seconds += (travelMm(move, from) / rate) * 60
    }
    from = move.to
  }
  return seconds + path.cycleMinutes * 60
}

/** The stable identifier of one operation inside a plan: `<uuid>/op<n>`, n counted from 1. */
export const operationUuid = (uuid: string, opIndex: number): string => `${uuid}/op${opIndex + 1}`

/** The moves of one operation, appended to `out`; the drilling cycle time comes back for the plan. */
const planOperation = (program: QuickProgram, operation: Operation, out: PathBuilder): number => {
  switch (operation.kind) {
    case 'face':
      planFace(program, operation, out)
      return 0
    case 'pocket':
      planRectPocket(program, operation, out)
      return 0
    case 'circle-pocket':
      planCirclePocket(program, operation, out)
      return 0
    case 'drill': {
      const plan = planDrill(program, operation, out)
      return drillCycleMinutes(operation, plan.points.length)
    }
    case 'contour':
      planContour(program, operation, out)
      return 0
    case 'slot':
      planSlot(program, operation, out)
      return 0
    case 'chamfer':
      planChamfer(program, operation, out)
      return 0
    case 'tap': {
      const plan = planTap(program, operation, out)
      return tapCycleMinutes(operation, plan.points.length)
    }
    case 'counterbore':
      // A cycle is the machine's: the seats are charged to `cycleMinutes` (`bores.ts`).
      planCounterbore(program, operation, out)
      return counterboreMinutes(program, operation)
    case 'thread':
      // The helix is a path: its arcs are charged move by move in `estimateSeconds`.
      planThread(program, operation, out)
      return 0
    case 'custom':
      // The lines are the program: no move is planned and the viewer parser is their only check.
      return 0
    case 'probe':
      // A touch touches the part, it does not cut it: the emitter writes the G38.2 block and the
      // planner draws no move, so the path and the estimate stay the cutting ones.
      return 0
  }
}

/** The document index of the operation a move stands in, by its position in the path. */
const operationOf = (spans: OperationSpan[], at: number): number => {
  let found = 0
  for (const span of spans) {
    if (span.first > at) break
    found = span.opIndex
  }
  return found
}

/**
 * The plan of a program: the document is validated first, then every
 * enabled operation is planned in order. The uuid of the path comes from `path-uuid.ts`, so a
 * golden file has the same `UI_STAGE` and a stale preview can be told from the fresh one.
 */
export function planProgram(program: QuickProgram): PlanResult {
  const checked = validate(program)
  if (!checked.ok) return { ok: false, problems: checked.bad.map((item) => `${item.field}: ${item.reason}`) }
  const out = new PathBuilder()
  const operations: OperationSpan[] = []
  let cycleMinutes = 0
  program.operations.forEach((operation, opIndex) => {
    if (!operation.enabled) return
    const first = out.moves.length
    cycleMinutes += planOperation(program, operation, out)
    operations.push({ opIndex, first })
  })
  // Every move names the operation it belongs to (`operations` index in the document), so the
  // collision check binds a problem to the operator's list entry without a second span lookup.
  const cut = out.result()
  const moves = cut.moves.map((move, at) => ({ ...move, operation: operationOf(operations, at) }))
  const path: ProgramPath = { ...cut, moves, operations, cycleMinutes }
  return { ok: true, path }
}

/** The plan of a program; the path carries the uuid from `path-uuid.ts`. */
export function buildProgram(program: QuickProgram): PlanResult {
  return planProgram(program)
}
