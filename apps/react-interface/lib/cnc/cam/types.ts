// The types of the quick-program overlay, apart from the reducer (PLAN-QUICKPROG, 6 and 7.4).
//
// `types.ts` at the root of `lib/cnc` holds `MachineState`, and its `cam` branch needs `CamState`.
// Importing the reducer there would pull `cam.ts` and through it `files.ts` into the type graph of
// every module that names the machine; the note in CLAUDE.md, section 2, wants the type layer
// free of the logic. So the shapes of the overlay live here, with the types of the document and of
// the form and no import of a value: the reducer imports them as well and re-exports `CamState`,
// so the modules that say `./cam/cam` keep working.
import type { OperationKind, QuickProgram } from './ops'
import type { CamForm } from './form'

/** The overlay state: the document, the form being edited, the last preview and the file written. */
export interface CamState {
  program: QuickProgram
  // The document is built by `newProgram` (`defaults.ts`); naming its public type here is what lets
  // this module stay a leaf of the type graph and keeps `newProgram` in one place.
  /** The form of a new or edited operation; null when the operator is looking at the list. */
  form: CamForm | null
  /** The index of the operation the form edits; null while the form builds a new one. */
  editing: number | null
  /** The last problem of the form, bound to the field the operator sees. */
  error: { field: string; reason: string } | null
  /** The lines the preview draws: the emitted file of the current document. */
  preview: string[] | null
  /**
   * The moment the preview was last recomputed (`CAM_PREVIEW` reads it from the action, never from
   * the reducer). Absent until the first recompute of the overlay.
   */
  previewAt?: number
  /**
   * A field edit arrived and the preview is older than the form. `CAM_PREVIEW` recomputes and clears
   * it; every other action that recomputes does the same, so the flag never lies about the lines.
   */
  pending?: boolean
  /** The name of the file the last `CAM_SAVE` wrote; the next save replaces it. */
  saved: string | null
  /**
   * The file the last save found already present, the moment of that press and the document the
   * operator confirmed. The next save of that same document within `SAVE_CONFIRM_MS` replaces the
   * file; any other save asks again.
   */
  pendingSave: { name: string; at: number; program: QuickProgram } | null
}

/** The `CAM_*` actions of the registry; the reducer switches on them by kind. */
export type CamAction =
  | { type: 'CAM_OPEN' }
  | { type: 'CAM_CLOSE' }
  | { type: 'CAM_OP'; kind: OperationKind }
  | { type: 'CAM_PARAM'; field: string; value: string }
  | { type: 'CAM_PREVIEW'; at?: number }
  | { type: 'CAM_CANCEL' }
  | { type: 'CAM_ACCEPT' }
  | { type: 'CAM_EDIT'; index: number }
  | { type: 'CAM_REMOVE'; index: number }
  | { type: 'CAM_TOGGLE'; index: number }
  | { type: 'CAM_MOVE'; index: number; up: boolean }
  | { type: 'CAM_SAVE'; at?: number }
  // The copies of an operation are flattened into the list at once (PLAN-QUICKPROG, 7.6): a grid
  // `cols x rows` with its own step, or `copies` turns of `angleDeg` around `(cx, cy)`.
  | { type: 'CAM_ARRAY'; index: number; cols: number; rows: number; dx: number; dy: number }
  | { type: 'CAM_ROTATE'; index: number; angleDeg: number; cx: number; cy: number; copies: number }
