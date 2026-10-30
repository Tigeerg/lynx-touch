// The fields an operation is created from (PLAN-QUICKPROG, 7.4).
//
// The form fills the fields it shows; the operation is created from these named values, so the
// defaults are not repeated in the form and the reducer. The operation itself lives in `ops.ts`.
import type { DrillBody, Material, ToolRef } from './ops'

/** The fields a caller sets when it creates an operation; the rest come from the defaults. */
export interface NewOperation {
  id: string
  topZ: number
  depthMm?: number
  stepDownMm?: number
  tool?: ToolRef
  material?: Material
  body?: DrillBody
  peckMm?: number
}
