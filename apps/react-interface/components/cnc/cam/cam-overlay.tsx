'use client'

// The quick-program overlay of the «Программа» screen (PLAN-QUICKPROG, 7.4, C5).
//
// No sixth tab: the overlay sits over the screen (as `help-overlay.tsx` does), the softkey «Быстрая
// УП», F4 and Esc close it (`keys-route.ts`). The panel variant shows three columns — list, form,
// preview; the touch variant shows one of them at a time, chosen by the segments of the header,
// because 864 px of portrait width cannot hold three readable columns.
//
// The header carries what the operator checks before a save: the program, the work offset, the
// smoothing tolerance and the stock. A refusal of the save is a line in the header, not a modal on
// top of the form: the number that is wrong stays visible.
import { useState, type Dispatch } from 'react'
import { Save, TriangleAlert, X } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { CamState } from '@/lib/cnc/cam/cam'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { useUiMode } from '../ui-mode-context'
import { CamForm } from './cam-form'
import { headerText } from './cam-labels'
import { CamPreview } from './cam-preview'
import { OperationList } from './operation-list'
import { OperationPicker } from './operation-picker'

type Column = 'list' | 'form' | 'preview'

/**
 * The line under the header, in the order the operator needs it: a save waiting for the second
 * press, then a problem of the form, then the file the last save wrote.
 */
const statusLine = (pending: CamState['pendingSave'], warning: string | null, saved: string | null): string => {
  if (pending !== null) return `Нажмите ещё раз для замены ${pending.name}`
  if (warning !== null) return warning
  return `УП записана в файл ${saved ?? ''}`
}

const SEGMENTS: { id: Column; label: string }[] = [
  { id: 'list', label: 'Список' },
  { id: 'form', label: 'Форма' },
  { id: 'preview', label: 'Вид' },
]

export function CamOverlay({ state, dispatch }: { state: MachineState; dispatch: Dispatch<Action> }) {
  const cam = state.cam
  if (cam === null) return null
  return <CamOverlayBody cam={cam} state={state} dispatch={dispatch} />
}

function CamOverlayBody({ cam, state, dispatch }: { cam: CamState; state: MachineState; dispatch: Dispatch<Action> }) {
  const touch = useUiMode()?.mode === 'touch'
  // The column of the touch variant: the form opens as soon as there is one, so a tile press
  // leads straight to the numbers.
  const [picked, setPicked] = useState<Column | null>(null)
  // Until the operator taps a segment the form answers the state: a tile press on the list opens
  // the numbers, and «Добавить операцию» returns to the list.
  const column: Column = picked ?? (cam.form === null ? 'list' : 'form')
  const { wcs, g64, stock } = headerText(cam.program)
  const warning = cam.error?.reason ?? null
  const saved = cam.saved
  // The file the last save found in the way (PLAN-QUICKPROG, 7.5): the header asks for one more
  // press and the save button is «Заменить» while the confirmation stands.
  const pending = cam.pendingSave

  // The picked segment only wins while it still shows the form: a tile press and «Отмена» switch
  // to the state-driven column, so the form appears and goes away without a second tap.
  const show = (id: Column) => setPicked(id === 'form' || id === 'list' ? null : id)
  const close = () => dispatch({ type: 'CAM_CLOSE' })
  // The moment of the press comes from the action, not from the reducer: the window of the
  // confirmation is measured by the second press against this stamp.
  const save = () => dispatch({ type: 'CAM_SAVE', at: Date.now() })
  const busy = state.run === 'RUN' || state.run === 'HOLD'

  const list = <OperationList program={cam.program} editing={cam.editing} dispatch={dispatch} />
  const form =
    cam.form === null ? (
      <section aria-label="Форма операции" className="flex min-h-0 flex-1 flex-col justify-center">
        <OperationPicker dispatch={dispatch} />
      </section>
    ) : (
      <CamForm form={cam.form} editing={cam.editing !== null} problem={cam.error} dispatch={dispatch} />
    )
  const preview = <CamPreview preview={cam.preview} />

  return (
    <div className="absolute inset-0 z-40 flex items-center justify-center bg-foreground/30 p-1.5">
      <section
        role="dialog"
        aria-modal="true"
        aria-labelledby="cam-title"
        className="@container flex h-[580px] max-h-full w-full flex-col overflow-hidden rounded-md border bg-card shadow-xl"
      >
        <header className="flex shrink-0 flex-col gap-1 border-b bg-muted px-2 py-1.5">
          <div className="flex items-center gap-2">
            <h2 id="cam-title" className="text-sm font-bold uppercase tracking-wide">
              Быстрая УП
            </h2>
            <span className="min-w-0 truncate text-xs text-muted-foreground">{cam.program.name}</span>
            <span className="rounded-sm border bg-background px-1.5 py-0.5 font-mono text-[10px] font-bold">{wcs}</span>
            <span className="hidden shrink-0 rounded-sm border bg-background px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground @[900px]:inline">
              {g64}
            </span>
            <span className="hidden min-w-0 truncate rounded-sm border bg-background px-1.5 py-0.5 font-mono text-[10px] text-muted-foreground @[620px]:inline">
              Заготовка {stock}
            </span>
            <div className="ml-auto flex shrink-0 items-center gap-1.5">
              <button
                type="button"
                tabIndex={-1}
                onClick={save}
                title={
                  busy
                    ? 'Остановите программу перед записью УП'
                    : pending
                      ? `Нажмите ещё раз, чтобы заменить ${pending.name}`
                      : 'Записать .ngc в список файлов'
                }
                className={cn(
                  'flex items-center gap-1.5 rounded-md border-2 border-primary bg-primary px-2.5 text-xs font-bold text-primary-foreground transition-opacity hover:opacity-90',
                  touch ? 'min-h-11' : 'min-h-8',
                )}
              >
                <Save className="size-4" aria-hidden="true" />
                {pending ? 'Заменить' : 'Сохранить УП'}
              </button>
              <button
                type="button"
                tabIndex={-1}
                onClick={close}
                className={cn(
                  'flex items-center gap-1.5 rounded-md border-2 bg-card px-2.5 text-xs font-bold transition-colors hover:bg-accent',
                  touch ? 'min-h-11' : 'min-h-8',
                )}
              >
                <X className="size-4" aria-hidden="true" />
                Закрыть
              </button>
            </div>
          </div>
          {(pending ?? warning ?? saved) && (
            <p
              role="status"
              className={cn(
                'flex min-w-0 items-center gap-1.5 text-[10px] font-semibold',
                warning ? 'text-destructive' : pending ? 'text-warning' : 'text-success',
              )}
            >
              {(pending || warning) && <TriangleAlert className="size-3 shrink-0" aria-hidden="true" />}
              <span className="truncate">{statusLine(pending, warning, saved)}</span>
            </p>
          )}
          {touch && (
            <div role="tablist" aria-label="Часть формы" className="flex overflow-hidden rounded-md border">
              {SEGMENTS.map((s) => (
                <button
                  key={s.id}
                  type="button"
                  role="tab"
                  tabIndex={-1}
                  aria-selected={column === s.id}
                  onClick={() => show(s.id)}
                  className={cn(
                    'flex min-h-11 flex-1 items-center justify-center text-xs font-semibold transition-colors',
                    column === s.id ? 'bg-primary text-primary-foreground' : 'bg-card hover:bg-accent',
                  )}
                >
                  {s.label}
                </button>
              ))}
            </div>
          )}
        </header>

        {touch ? (
          <div className="flex min-h-0 flex-1 flex-col gap-1.5 p-1.5">
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
      </section>
    </div>
  )
}
