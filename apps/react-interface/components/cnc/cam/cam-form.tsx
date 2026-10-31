'use client'

// The form of one operation (PLAN-QUICKPROG, 7.4, C5 and 7.6): three tabs from `cam-fields.ts`, an
// input per field (`field-row.tsx`), the reason of a failed check under the number it talks about
// and the warnings of the kind under that. The tab state and the shown rows live in
// `use-form-state.ts`, so this file stays the page. Every edit is `CAM_PARAM`; the document changes
// only on «Добавить в программу».
//
// The form keeps no local copy of the values: `CamForm.values` in the state is the single source,
// so a button that fires twice cannot show a stale number. The preview is the exception: a field
// edit applies at once and the preview waits 150 ms for the last keystroke (`CAM_PREVIEW`).
import { useEffect, useRef } from 'react'
import { CircleCheck, TriangleAlert } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { CamForm } from '@/lib/cnc/cam/form'
import type { QuickProgram } from '@/lib/cnc/cam/ops'
import { needsPreview, previewTimer, waitsForPreview } from '@/lib/cnc/cam/cam-debounce'
import { cn } from '@/lib/utils'
import { useUiMode } from '../ui-mode-context'
import { axisOf, CAM_TAB_LABEL, droFieldOf, type CamTab } from './cam-fields'
import { FieldRow } from './field-row'
import { Keypad } from './keypad'
import { KIND_LABEL } from './cam-labels'
import { useFormState } from './use-form-state'

const TABS: CamTab[] = ['geometry', 'tool', 'modes']

export function CamForm({
  form,
  program,
  editing,
  problem,
  pending,
  dispatch,
}: {
  form: CamForm
  /** The document: the `source` field of a rest offers its pocket operations. */
  program: QuickProgram
  editing: boolean
  problem: { field: string; reason: string } | null
  /** The preview of this form is older than the typed text: a timer will recompute it. */
  pending: boolean
  dispatch: (a: Action) => void
}) {
  const touch = useUiMode()?.mode === 'touch'
  // One timer per form: a new keystroke moves it, leaving the form cancels it, so a closed form
  // cannot send `CAM_PREVIEW` about numbers nobody shows any more.
  const timer = useRef(previewTimer())
  const formRef = useRef(form)
  formRef.current = form
  const preview = () => dispatch({ type: 'CAM_PREVIEW', at: Date.now() })
  useEffect(() => {
    if (pending) timer.current.arm(preview)
  }, [pending])
  // Leaving the form (accept, cancel, another operation) drops the timer: no `CAM_PREVIEW` follows
  // numbers the operator no longer sees.
  useEffect(() => () => timer.current.cancel(), [])
  const { tab, setTab, active, setActive, errors, kindWarnings, others, shown } = useFormState(form)
  const warning = problem?.reason ?? null

  return (
    <section aria-label="Форма операции" className="flex min-h-0 flex-1 flex-col gap-1.5">
      <header className="flex shrink-0 items-center gap-1.5">
        <h3 className="min-w-0 flex-1 truncate text-xs font-bold">
          {editing ? 'Правка' : 'Новая операция'} · {KIND_LABEL[form.op]}
        </h3>
      </header>

      <div role="tablist" aria-label="Вкладки формы" className="flex shrink-0 overflow-hidden rounded-md border">
        {TABS.map((t) => (
          <button
            key={t}
            type="button"
            role="tab"
            tabIndex={-1}
            aria-selected={t === tab}
            onClick={() => setTab(t)}
            className={cn(
              'flex-1 text-[11px] font-semibold transition-colors',
              touch ? 'min-h-11' : 'min-h-8',
              t === tab ? 'bg-primary text-primary-foreground' : 'bg-card text-muted-foreground hover:bg-accent',
            )}
          >
            {CAM_TAB_LABEL[t]}
          </button>
        ))}
      </div>

      <div className="flex min-h-0 flex-1 flex-col gap-1.5 overflow-y-auto pr-0.5">
        <fieldset className="flex flex-col gap-1.5">
          <legend className="sr-only">{CAM_TAB_LABEL[tab]}</legend>
          {shown.map((field) => (
            <FieldRow
              key={field.key}
              field={field}
              value={form.values[field.key] ?? ''}
              error={errors[field.key]}
              touch={touch}
              active={active === field.key}
              operations={program.operations}
              kind={form.op}
              onFocus={() => setActive(field.key)}
              onDro={() =>
                dispatch({ type: 'CAM_DRO', field: droFieldOf(form.op, field.key) ?? 'x', axis: axisOf(field.key) })
              }
              onChange={(value) => {
                dispatch({ type: 'CAM_PARAM', field: field.key, value })
                if (needsPreview(form, formRef.current)) timer.current.arm(preview)
              }}
            />
          ))}
        </fieldset>

        {others.length > 0 && (
          <ul className="flex flex-col gap-0.5 text-[10px] text-destructive" aria-label="Другие замечания">
            {others.map((f) => (
              <li key={f.key}>
                {f.label}: {errors[f.key]}
              </li>
            ))}
          </ul>
        )}

        {warning && !form.error && (
          <p className="flex items-start gap-1 text-[10px] text-destructive" role="status">
            <TriangleAlert className="mt-px size-3 shrink-0" aria-hidden="true" />
            {warning}
          </p>
        )}

        {kindWarnings.length > 0 && (
          <ul className="flex flex-col gap-0.5 text-[10px] text-warning" aria-label="Предупреждения операции">
            {kindWarnings.map((reason) => (
              <li key={reason} className="flex items-start gap-1">
                <TriangleAlert className="mt-px size-3 shrink-0" aria-hidden="true" />
                {reason}
              </li>
            ))}
          </ul>
        )}
      </div>

      {touch && <Keypad field={active} value={active ? (form.values[active] ?? '') : null} dispatch={dispatch} />}

      <footer className="flex shrink-0 gap-1.5">
        <button
          type="button"
          tabIndex={-1}
          disabled={form.error !== null}
          onClick={() => dispatch({ type: 'CAM_ACCEPT' })}
          className={cn(
            'flex flex-1 items-center justify-center gap-1.5 rounded-md border-2 border-primary bg-primary text-xs font-bold text-primary-foreground transition-opacity hover:opacity-90 disabled:opacity-40',
            touch ? 'min-h-12' : 'min-h-9',
          )}
        >
          <CircleCheck className="size-4" aria-hidden="true" />
          {editing ? 'Сохранить изменения' : 'Добавить в программу'}
        </button>
        <button
          type="button"
          tabIndex={-1}
          onClick={() => dispatch({ type: 'CAM_CANCEL' })}
          className={cn(
            'flex items-center justify-center rounded-md border-2 bg-card px-3 text-xs font-bold transition-colors hover:bg-accent',
            touch ? 'min-h-12' : 'min-h-9',
          )}
        >
          Отмена
        </button>
      </footer>
    </section>
  )
}
