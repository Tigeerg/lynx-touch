// The estimate of the overlay header (PLAN-QUICKPROG, 5 step 9): the moves, the cutting length and
// the time of the preview, as one sentence and as numbers.
//
// The numbers are not recomputed here. They come from the planned path the preview was emitted from
// (`buildProgram`, `estimateSeconds`), and are remembered in `CamState.stats` by every action that
// recomputes the preview. Reading the G-code by eye is where the header and the planner would drift
// apart; this module only says what the plan has, and `cam-labels.ts` prints it.
import { buildProgram, estimateSeconds, type ProgramPath } from './path'
import type { QuickProgram } from './ops'

/** What the header says about the program: the plan's own numbers. */
export interface CamStats {
  moves: number
  /** The cutting length of the moves, mm. */
  lengthMm: number
  /** The time estimate of the plan, seconds (`estimateSeconds`). */
  seconds: number
}

/** The stats of a planned path: the move count, the cut length and the estimated time. */
export const statsOf = (path: ProgramPath): CamStats => ({
  moves: path.moves.length,
  lengthMm: path.lengthMm,
  seconds: estimateSeconds(path),
})

/**
 * The stats of a document, or null when it cannot be planned. The header asks this once per
 * recomputed preview; the reducer that already has the plan passes it to `statsOf` instead.
 */
export function programStats(program: QuickProgram): CamStats | null {
  const built = buildProgram(program)
  return built.ok ? statsOf(built.path) : null
}
