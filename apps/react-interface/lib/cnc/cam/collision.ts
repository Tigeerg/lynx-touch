// The collision check of PLAN-QUICKPROG, 7.5: the panel's own simple rule, no viewer.
//
// The check reads the planned path and the document, never the machine: every point the tool centre
// stands at is compared with the stock grown by `D / 2` in plan (the disc of the tool has to stay
// inside the stock). A centre outside that box is a warning: the tool overhangs the stock and the
// cut may be open. The depth itself is the business of `validate` (`depthMm`), which already keeps a
// pass above the stock bottom: the check would never fire on a document that plans, and a second
// copy of the rule would drift from the first.
//
// The plan rule does not apply to a face operation: facing overruns the edge by its lead by
// construction, and its overhang is rated on its own. The Z rule applies to every kind.
//
// The result names the operator's list entry: every issue carries `operations[<index>]`, the index
// of the operation in the document (taken from `CamMove.operation`, written by `path.ts`), so the
// form can point at the entry.
//
// The check is UNVERIFIED against the real geometry of the part: it knows the stock box and the
// tool disc, not the part, the clamps or the fixture.
import { fmt } from './feeds'
import type { CamMove, CamPath } from './moves'
import type { Operation, QuickProgram } from './ops'

/** One finding of the check: the field of the operator's list and the reason in the operator's words. */
export interface Issue {
  field: string
  reason: string
}

/** The findings of a program: what blocks the save and what only warns about it. */
export interface CollisionResult {
  errors: Issue[]
  warnings: Issue[]
}

/** The tolerance of the comparisons, mm: the document's own tolerance, so the check agrees with it. */
const EPS = 1e-9

/** The four points of a quarter turn a real arc may reach in plan. */
const QUARTERS = [0, Math.PI / 2, Math.PI, (3 * Math.PI) / 2]

/**
 * The points of a move a disc of the tool may stand at in plan: the end point of a line, and the
 * quarter points of an arc around its centre. An arc is never judged by its end alone.
 */
const planPoints = (move: CamMove, from: { x: number; y: number }): { x: number; y: number }[] => {
  if (move.arc === undefined) return [move.to]
  const cx = from.x + move.arc.i
  const cy = from.y + move.arc.j
  const r = Math.hypot(move.arc.i, move.arc.j)
  return QUARTERS.map((angle) => ({ x: cx + r * Math.cos(angle), y: cy + r * Math.sin(angle) }))
}

/** True when the tool centre stands outside the stock grown by the tool radius in plan. */
const outsidePlan = (program: QuickProgram, point: { x: number; y: number }, radius: number): boolean => {
  const { min, max } = program.stock
  return (
    point.x < min.x - radius - EPS ||
    point.x > max.x + radius + EPS ||
    point.y < min.y - radius - EPS ||
    point.y > max.y + radius + EPS
  )
}

/** The field of one operation of the document. */
const fieldOf = (index: number): string => `operations[${String(index)}]`

/** The plan reason of one operation, or null when every point of its moves fits the stock. */
const planIssue = (program: QuickProgram, operation: Operation, index: number, moves: CamMove[]): Issue | null => {
  // Facing overruns the edge by its lead by construction; the plan rule is not a facing rule.
  if (operation.kind === 'face') return null
  const radius = operation.tool.diameterMm / 2
  let from = { x: 0, y: 0 }
  for (const move of moves) {
    for (const point of planPoints(move, from)) {
      if (outsidePlan(program, point, radius)) {
        return {
          field: fieldOf(index),
          reason:
            `Инструмент выходит за заготовку в плане (операция ${String(index + 1)}): ` +
            `X${fmt(point.x)} Y${fmt(point.y)}`,
        }
      }
    }
    from = move.to
  }
  return null
}

/** The moves of every operation, by the operation index each move carries (`path.ts` writes it). */
const movesByOperation = (path: CamPath): Map<number, CamMove[]> => {
  const groups = new Map<number, CamMove[]>()
  for (const move of path.moves) {
    const index = move.operation
    if (index === undefined) continue
    const group = groups.get(index)
    if (group === undefined) groups.set(index, [move])
    else group.push(move)
  }
  return groups
}

/**
 * The findings of a planned program. The caller runs it only on a path that `buildProgram` built
 * from a valid document: a document that does not plan has no path and the validation speaks first.
 * An empty list of operations is an error, so the save cannot write an empty file.
 *
 * A disabled operation plans no moves; it still takes its place in the list, so the index of an
 * enabled operation in the document is the one in the message.
 */
export function checkProgram(program: QuickProgram, path: CamPath): CollisionResult {
  const errors: Issue[] = []
  const warnings: Issue[] = []
  if (program.operations.length === 0) {
    errors.push({ field: 'operations', reason: 'В программе нет операций' })
    return { errors, warnings }
  }
  const groups = movesByOperation(path)
  for (const [index, moves] of groups) {
    const operation = program.operations[index]
    if (operation === undefined || !operation.enabled) continue
    const plan = planIssue(program, operation, index, moves)
    if (plan !== null) warnings.push(plan)
  }
  return { errors, warnings }
}
