'use client'

// The DXF import dialog (PLAN-QUICKPROG, 7.6, «Импорт DXF»): a file input, the contours of the file
// as a list with their numbers, the warning of the skipped entities and one «Добавить контур»
// button that sends `CAM_DXF { text, contour }`.
//
// The file is not kept in the state: the dialog reads the text once into its own `reading`, sends
// it with the action on the press and drops it with the dialog. The panel and the touch read the
// file the same way (`File.text()`), so the two variants differ only in the size of the rows.
// The words are `dxf-dialog-text.ts`; this file is the page.
import { useId, useState } from 'react'
import { FileUp, TriangleAlert, X } from 'lucide-react'
import type { Dispatch } from 'react'
import type { CamAction } from '@/lib/cnc/cam/cam'
import { readDxf, type DxfReading } from '@/lib/cnc/cam/dxf-source'
import { cn } from '@/lib/utils'
import { useUiMode } from '../ui-mode-context'
import {
  DXF_AFTER_ADD,
  DXF_PICK_HINT,
  dxfAddLabel,
  dxfError,
  dxfRows,
  dxfSummary,
  dxfWarning,
  type DxfRow,
} from './dxf-dialog-text'

export function DxfDialog({ dispatch, onClose }: { dispatch: Dispatch<CamAction>; onClose: () => void }) {
  const touch = useUiMode()?.mode === 'touch'
  const titleId = useId()
  // The picked row and the read file: both leave with the dialog, and the file text never reaches
  // the machine state — it travels once, inside the `CAM_DXF` action of the press.
  const [text, setText] = useState<string | null>(null)
  const [reading, setReading] = useState<DxfReading | null>(null)
  const [picked, setPicked] = useState<number | null>(null)
  const rows: DxfRow[] = reading === null ? [] : dxfRows(reading)
  const chosen = rows.find((row) => row.index === picked) ?? null
  const error = reading === null ? null : dxfError(reading)
  const warning = reading === null ? null : dxfWarning(reading)

  const load = async (file: File | undefined) => {
    if (file === undefined) return
    const data = await file.text()
    const next = readDxf(data)
    setText(data)
    setReading(next)
    // A single contour is the usual file: the operator still presses «Добавить контур», so the
    // action is one press and never fires on a file pick.
    const list = next.ok ? next.contours : []
    setPicked(list.length === 1 ? 0 : null)
  }

  const add = () => {
    if (text === null || chosen === null) return
    dispatch({ type: 'CAM_DXF', text, contour: chosen.index })
    onClose()
  }

  return (
    <div className="absolute inset-0 z-50 flex items-center justify-center bg-foreground/30 p-2">
      <section
        role="dialog"
        aria-modal="true"
        aria-labelledby={titleId}
        className="flex max-h-full w-[22rem] flex-col gap-1.5 rounded-md border bg-card p-2 shadow-xl"
      >
        <header className="flex items-center gap-1.5">
          <FileUp className="size-4 shrink-0 text-primary" aria-hidden="true" />
          <h3 id={titleId} className="min-w-0 flex-1 truncate text-xs font-bold">
            Импорт DXF
          </h3>
          <button
            type="button"
            tabIndex={-1}
            aria-label="Закрыть"
            onClick={onClose}
            className={cn(
              'flex items-center justify-center rounded-sm border bg-background hover:bg-accent',
              touch ? 'size-11' : 'size-6',
            )}
          >
            <X className="size-4" aria-hidden="true" />
          </button>
        </header>

        <p className="text-[10px] leading-tight text-muted-foreground">{DXF_PICK_HINT}</p>
        <input
          type="file"
          accept=".dxf,text/plain"
          tabIndex={-1}
          aria-label="Файл DXF"
          onChange={(event) => {
            void load(event.target.files?.[0])
            // The same file twice in a row is the same change: reset, so picking it again reads it again.
            event.target.value = ''
          }}
          className={cn('w-full min-w-0 text-[10px]', touch ? 'min-h-11' : 'h-8')}
        />

        {reading !== null && (
          <p className="text-[10px] font-semibold text-muted-foreground" role="status">
            {dxfSummary(reading)}
          </p>
        )}

        {error !== null ? (
          <p className="flex items-start gap-1 text-[10px] leading-tight text-destructive" role="alert">
            <TriangleAlert className="mt-px size-3 shrink-0" aria-hidden="true" />
            {error}
          </p>
        ) : (
          rows.length > 0 && (
            <ul aria-label="Контуры файла" className="flex max-h-48 min-h-0 flex-col gap-1 overflow-y-auto">
              {rows.map((row) => (
                <li key={row.index}>
                  <button
                    type="button"
                    tabIndex={-1}
                    aria-pressed={row.index === picked}
                    onClick={() => setPicked(row.index)}
                    className={cn(
                      'flex w-full min-w-0 items-center gap-1.5 rounded-sm border px-1.5 text-left transition-colors hover:bg-accent',
                      touch ? 'min-h-11' : 'min-h-7',
                      row.index === picked ? 'border-primary bg-primary/10' : 'border-border bg-background',
                    )}
                  >
                    <span className="flex size-5 shrink-0 items-center justify-center rounded-sm bg-muted font-mono text-[10px] font-bold text-muted-foreground">
                      {row.number}
                    </span>
                    <span className="min-w-0 flex-1 truncate font-mono text-[10px] tabular-nums">{row.text}</span>
                  </button>
                </li>
              ))}
            </ul>
          )
        )}

        {warning !== null && (
          <p className="flex items-start gap-1 text-[10px] leading-tight text-warning" role="status">
            <TriangleAlert className="mt-px size-3 shrink-0" aria-hidden="true" />
            {warning}
          </p>
        )}
        <p className="text-[10px] leading-tight text-muted-foreground">{DXF_AFTER_ADD}</p>

        <div className="mt-0.5 flex gap-1.5">
          <button
            type="button"
            tabIndex={-1}
            disabled={chosen === null}
            onClick={add}
            className={cn(
              'flex flex-1 items-center justify-center gap-1.5 rounded-md border-2 border-primary bg-primary text-xs font-bold text-primary-foreground hover:opacity-90 disabled:opacity-40',
              touch ? 'min-h-12' : 'min-h-9',
            )}
          >
            <FileUp className="size-4" aria-hidden="true" />
            {dxfAddLabel(chosen, rows.length)}
          </button>
          <button
            type="button"
            tabIndex={-1}
            onClick={onClose}
            className={cn(
              'flex items-center justify-center rounded-md border-2 bg-card px-3 text-xs font-bold hover:bg-accent',
              touch ? 'min-h-12' : 'min-h-9',
            )}
          >
            Отмена
          </button>
        </div>
      </section>
    </div>
  )
}
