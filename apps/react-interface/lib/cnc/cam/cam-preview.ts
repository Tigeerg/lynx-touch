// The emitted lines and the stats of a preview (PLAN-QUICKPROG, 5 step 9 and 7.5): one plan, two
// views. The overlay draws the lines and the header prints the numbers of the same plan.
//
// Every action that recomputes the preview takes both from here, so the header's «ходов · длина ·
// время» describes the very program the operator sees. A document that does not plan has no lines
// and no stats; the caller keeps what it had.
import { emitOf } from './cam-save'
import { formOperation } from './form'
import type { QuickProgram } from './ops'
import { buildProgram } from './path'
import { statsOf, type CamStats } from './cam-stats'
import type { CamState } from './types'

/** One preview: the lines the operator sees and the numbers of the plan behind them. */
export interface Preview {
  lines: string[]
  stats: CamStats | null
}

/** The preview of the lines and the stats of a plan; null when either half cannot be built. */
export const previewOfPath = (program: QuickProgram): Preview | null => {
  const built = buildProgram(program)
  if (!built.ok) return null
  const lines = emitOf(program)
  return lines === null ? null : { lines, stats: statsOf(built.path) }
}

/** The document with the form's operation, as the preview and the accept build it. */
export const formDocument = (cam: CamState): QuickProgram | null => {
  if (cam.form === null) return null
  const editing = cam.editing === null ? null : (cam.program.operations[cam.editing] ?? null)
  return { ...cam.program, operations: [...cam.program.operations, formOperation(cam.form, editing)] }
}

/** The preview of the form, or the lines and stats the overlay already had when the form does not plan. */
export const formPreview = (cam: CamState): Preview => {
  const program = formDocument(cam)
  const preview = program === null ? null : previewOfPath(program)
  return preview ?? { lines: cam.preview ?? [], stats: cam.stats ?? null }
}

/** The lines and the stats as the overlay stores them; `keep` when the document does not plan. */
export const shownPreview = (
  program: QuickProgram,
  keep: { preview: string[] | null; stats?: CamStats | null },
): { preview: string[] | null; stats: CamStats | null } => {
  const preview = previewOfPath(program)
  return preview === null
    ? { preview: keep.preview, stats: keep.stats ?? null }
    : { preview: preview.lines, stats: preview.stats }
}

/**
 * The one recompute of the overlay (PLAN-QUICKPROG, 5 step 9): the header's lines and numbers of
 * the very program the screen shows. While the form is open that program is the document plus the
 * form's operation; without it, the document alone. Every action that changes the plan — the accept,
 * the list, the copies, the header of the document, the save — takes its preview and stats from
 * here, so a header never describes another program than the one on the screen.
 *
 * `withoutForm` says the caller is about to close the form and the document itself is the program
 * the operator will see (the accepting save): nothing of the form is added.
 */
export const recompute = (
  cam: CamState,
  program: QuickProgram = cam.program,
  withoutForm = false,
): { preview: string[] | null; stats: CamStats | null } => {
  const shown = withoutForm ? { ...cam, program, form: null } : { ...cam, program }
  if (shown.form === null) return shownPreview(program, cam)
  const withForm = formDocument(shown)
  const preview = withForm === null ? null : previewOfPath(withForm)
  return preview === null
    ? { preview: cam.preview ?? [], stats: cam.stats ?? null }
    : { preview: preview.lines, stats: preview.stats }
}
