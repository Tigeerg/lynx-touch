// The two edits of the overlay that are not a keystroke of the form (PLAN-QUICKPROG, 5 steps 1-2
// and 6): the work coordinate «С DRO» and the header of the document (the offset, the stock, the
// safe Z, the name). Both live here so `cam.ts` stays the reducer and the table of actions.
//
// The coordinate goes through the same `edited` path as typed text, so the check and the debounced
// preview behave the same. The header is the document, not the form: its edit recomputes the
// preview at once and binds a refusal to the field the header shows.
import { toWork } from '../coords'
import type { Result } from '../subsystem'
import type { MachineState } from '../types'
import { droField, droText, isDroField } from './cam-dro'
import { edited, type Edit } from './cam-form-run'
import { formPreview, shownPreview } from './cam-preview'

import type { CamAction, CamState } from './types'
import { programEdit } from './cam-program'

/** The function the reducer's `edited` step has: the form with one typed value, its problem and the pending flag. */

/** `CAM_DRO`: the work coordinate of the display into the field of the form the pair names. */
export function droRun(s: MachineState, cam: CamState, field: string, edited: Edit): Result {
  if (!isDroField(field)) return { s, msg: `Поле «${field}» не берёт координату с DRO`, level: 'warn' }
  const key = droField(cam, field)
  if (key === null) return { s, msg: 'Сначала выберите операцию с этим полем', level: 'warn' }
  // The display the operator reads: the machine coordinate less the active work offset.
  const value = droText(toWork(s), field)
  return { s: { ...s, cam: edited(cam, key, value) }, msg: `С DRO: ${value}` }
}

/** `CAM_PROGRAM`: the header changed in the document; the preview catches up at once. */
export function programRun(s: MachineState, cam: CamState, field: string, value: string): Result {
  const change = programEdit(cam.program, field, value)
  if (change === null) return { s, msg: `В программе нет поля «${field}»`, level: 'warn' }
  const problem = change.problem
  const shown = shownPreview(change.program, cam)
  const next: CamState = { ...cam, program: change.program, ...shown, pending: false, error: problem }
  return {
    s: { ...s, cam: next },
    msg: problem === null ? `Программа: ${field}` : problem.reason,
    level: problem === null ? 'info' : 'warn',
  }
}

/** The actions of `amend`: a typed value, a coordinate, a header cell, the debounced preview, the cancel. */
export type Amending = Extract<
  CamAction,
  | { type: 'CAM_PARAM' }
  | { type: 'CAM_DRO' }
  | { type: 'CAM_PROGRAM' }
  | { type: 'CAM_PREVIEW' }
  | { type: 'CAM_CANCEL' }
>

/**
 * The edits of the form and of the header (PLAN-QUICKPROG, 5 and 7.5): a typed value or a
 * coordinate, a header cell, the preview catching up after the debounce and the cancel of the form.
 * The form types and its preview waits; the header is not typing and the preview catches up at once.
 */
export function amend(s: MachineState, cam: CamState, a: Amending): Result {
  switch (a.type) {
    case 'CAM_PARAM':
      return { s: { ...s, cam: edited(cam, a.field, a.value) } }
    case 'CAM_DRO':
      return droRun(s, cam, a.field, edited)
    case 'CAM_PROGRAM':
      return programRun(s, cam, a.field, a.value)
    case 'CAM_CANCEL':
      return { s: { ...s, cam: { ...cam, form: null, editing: null, error: null } } }
    case 'CAM_PREVIEW':
      // Without an unfinished edit there is nothing to catch up with; a late tick is a no-op.
      if (cam.pending !== true) return { s }
      const preview = formPreview(cam)
      const lines = preview.lines
      return {
        s: {
          ...s,
          cam: { ...cam, preview: lines, stats: preview.stats, pending: false, previewAt: a.at ?? cam.previewAt },
        },
      }
  }
}
