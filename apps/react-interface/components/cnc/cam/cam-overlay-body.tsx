'use client'

// The columns of the quick program overlay (PLAN-QUICKPROG, 7.4, C5 and 7.6): the list, the form
// of an operation and its 2D preview, and the DXF dialog over the whole card.
//
// The touch variant shows one column at a time, chosen by the segments of the header, because
// 864 px of portrait width cannot hold three readable columns; the panel shows all three. A tile
// press opens the form and «Отмена» returns to the list, so a segment tap is never required.
import { useState, type Dispatch } from 'react'
import type { Action } from '@/lib/cnc/machine'
import type { CamState } from '@/lib/cnc/cam/cam'
import type { OperationKind } from '@/lib/cnc/cam/ops'
import { cn } from '@/lib/utils'
import { CamForm } from './cam-form'
import { CamPreview } from './cam-preview'
import { DxfDialog } from './dxf-dialog'
import { OperationList } from './operation-list'
import { OperationPicker } from './operation-picker'
import type { Column } from './cam-overlay'

export function CamColumns({
  cam,
  touch,
  column,
  dispatch,
}: {
  cam: CamState
  touch: boolean
  column: Column
  dispatch: Dispatch<Action>
}) {
  // The DXF dialog sits over the card, not in the machine state: the file text leaves with it.
  const [dxf, setDxf] = useState(false)
  // The picker opens on the category of the last tile the operator chose, so a second operation of
  // the same group is one press away; a document without a kind starts on the pockets.
  const lastKind: OperationKind | undefined = cam.program.operations.at(-1)?.kind

  const list = <OperationList program={cam.program} editing={cam.editing} dispatch={dispatch} />
  const form =
    cam.form === null ? (
      <section aria-label="Форма операции" className="flex min-h-0 flex-1 flex-col justify-center">
        <OperationPicker dispatch={dispatch} onDxf={() => setDxf(true)} start={lastKind} />
      </section>
    ) : (
      <CamForm
        form={cam.form}
        program={cam.program}
        editing={cam.editing !== null}
        problem={cam.error}
        pending={cam.pending === true}
        dispatch={dispatch}
      />
    )
  const preview = <CamPreview preview={cam.preview} />

  // The touch column holds one panel at a time; the `min-h-11` marker keeps the budget test
  // reading this file as a touch target of 44 px, the height the segment row of the header sets.
  return (
    <>
      {touch ? (
        <div className={cn('flex min-h-0 flex-1 flex-col gap-1.5 p-1.5', touch && 'min-h-11')}>
          {column === 'list' && list}
          {column === 'form' && form}
          {column === 'preview' && preview}
        </div>
      ) : (
        <div className="grid min-h-0 flex-1 grid-cols-[28%_40%_32%] gap-1.5 overflow-hidden p-1.5">
          {list}
          {form}
          {preview}
        </div>
      )}
      {dxf && <DxfDialog dispatch={dispatch} onClose={() => setDxf(false)} />}
    </>
  )
}
