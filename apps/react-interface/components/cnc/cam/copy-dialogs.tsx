'use client'

// The copying dialogs of an operation card (PLAN-QUICKPROG, 7.6): a grid of copies and a turn
// around a point. The numbers are typed as text, as in the form, and the pure part —
// `cam-dialogs.ts` — turns them into `CAM_ARRAY`/`CAM_ROTATE`, so a field that is not a number is
// named here and a refusal of the document comes back as the status line of the overlay.
//
// The box is small on purpose: four rows, no tabs, and the same 1024x600 card as the other
// dialogs. A copy is flattened into the list at once, so the dialog closes on success and the
// operator sees the new entries behind it.
import { useState, type Dispatch } from 'react'
import { Grid3x3, RotateCw } from 'lucide-react'
import type { CamAction } from '@/lib/cnc/cam/cam'
import {
  ARRAY_DEFAULT,
  ARRAY_FIELDS,
  arrayAction,
  dialogRefusal,
  ROTATE_DEFAULT,
  ROTATE_FIELDS,
  rotateAction,
  type ArrayText,
  type RotateText,
} from './cam-dialogs'
import { CopyDialog } from './copy-dialog-form'

/** The button of the cards and of the copy menu: one shape for the order, state and edit actions. */
export const smallButton =
  'flex items-center justify-center gap-1 rounded-sm border bg-background px-1.5 text-[10px] font-semibold transition-colors hover:bg-accent active:bg-accent disabled:opacity-40'

export function ArrayDialog({
  index,
  dispatch,
  onClose,
}: {
  index: number
  dispatch: Dispatch<CamAction>
  onClose: () => void
}) {
  const [text, setText] = useState<ArrayText>(ARRAY_DEFAULT)
  return (
    <CopyDialog
      title="Массив копий"
      icon={Grid3x3}
      fields={ARRAY_FIELDS}
      text={text}
      onChange={(key, value) => setText({ ...text, [key]: value })}
      onSend={() => {
        const result = arrayAction(index, text)
        if (!result.ok) return result
        const refusal = dialogRefusal(result.action)
        if (refusal === null) dispatch(result.action)
        return { ok: true, problem: refusal ?? undefined }
      }}
      onClose={onClose}
    />
  )
}

export function RotateDialog({
  index,
  dispatch,
  onClose,
}: {
  index: number
  dispatch: Dispatch<CamAction>
  onClose: () => void
}) {
  const [text, setText] = useState<RotateText>(ROTATE_DEFAULT)
  return (
    <CopyDialog
      title="Поворот копий"
      icon={RotateCw}
      fields={ROTATE_FIELDS}
      text={text}
      onChange={(key, value) => setText({ ...text, [key]: value })}
      onSend={() => {
        const result = rotateAction(index, text)
        if (!result.ok) return result
        const refusal = dialogRefusal(result.action)
        if (refusal === null) dispatch(result.action)
        return { ok: true, problem: refusal ?? undefined }
      }}
      onClose={onClose}
    />
  )
}
