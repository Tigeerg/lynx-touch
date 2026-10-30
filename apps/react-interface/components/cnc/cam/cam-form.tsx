'use client'

// The form of one operation (PLAN-QUICKPROG, 7.4, C5): three tabs from `cam-fields.ts`, an input
// per field with its caption, unit and hint, and the reason of a failed check under the number it
// talks about. Every edit is `CAM_PARAM`; the document changes only on «Добавить в программу».
//
// The form keeps no local copy of the values: `CamForm.values` in the state is the single source,
// so a button that fires twice cannot show a stale number.
import { useState } from 'react'
import { CircleCheck, TriangleAlert } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { CamForm } from '@/lib/cnc/cam/form'
import { cn } from '@/lib/utils'
import { useUiMode } from '../ui-mode-context'
import {
  CAM_TAB_LABEL,
  CHOICE_LABEL,
  CHOICES,
  errorsByField,
  fieldGroups,
  patternFields,
  type CamTab,
} from './cam-fields'
import { Keypad } from './keypad'
import { KIND_LABEL } from './cam-labels'

const TABS: CamTab[] = ['geometry', 'tool', 'modes']

/** The keys of a tab, to tell a drill layout field from the ones every layout shares. */
const elementsOfTab = (tab: CamTab): string[] => fieldGroups('drill', tab).flatMap((g) => g.fields.map((f) => f.key))

/** A drill shows the fields of its layout only: a grid has no ring radius and a hole has no count. */
const isHidden = (key: string, tabKeys: string[], values: Record<string, string>): boolean =>
  tabKeys.includes('pattern') &&
  key !== 'pattern' &&
  key !== 'body' &&
  !patternFields(values.pattern ?? 'hole').includes(key)

export function CamForm({
  form,
  editing,
  problem,
  dispatch,
}: {
  form: CamForm
  editing: boolean
  problem: { field: string; reason: string } | null
  dispatch: (a: Action) => void
}) {
  const touch = useUiMode()?.mode === 'touch'
  const [tab, setTab] = useState<CamTab>('geometry')
  const [active, setActive] = useState<string | null>(null)
  const errors = errorsByField(form.error ? [form.error] : [])
  const groups = fieldGroups(form.op, tab)
  const keysOfTab = groups.flatMap((g) => g.fields.map((f) => f.key))
  const others = groups.flatMap((g) => g.fields).filter((f) => errors[f.key] && !keysOfTab.includes(f.key))
  const shown = groups
    .flatMap((g) => g.fields)
    .filter((f) => tab !== 'geometry' || form.op !== 'drill' || !isHidden(f.key, elementsOfTab(tab), form.values))
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
              onFocus={() => setActive(field.key)}
              onChange={(value) => dispatch({ type: 'CAM_PARAM', field: field.key, value })}
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

function FieldRow({
  field,
  value,
  error,
  touch,
  active,
  onFocus,
  onChange,
}: {
  field: { key: string; label: string; unit: string; hint: string }
  value: string
  error?: string
  touch: boolean
  active: boolean
  onFocus: () => void
  onChange: (value: string) => void
}) {
  const choice = CHOICES[field.key]
  return (
    <label className={cn('flex flex-col gap-0.5 rounded-sm px-1 py-0.5', active && 'bg-accent/60')}>
      <span className="flex items-baseline justify-between gap-1">
        <span className="text-[10px] font-semibold text-muted-foreground">
          {field.label}
          {field.unit && <span className="ml-1 font-normal">· {field.unit}</span>}
        </span>
      </span>
      {choice ? (
        <select
          value={value}
          tabIndex={-1}
          onChange={(e) => onChange(e.target.value)}
          className={cn(
            'rounded-sm border bg-background px-1.5 font-mono text-xs font-bold outline-none',
            touch ? 'min-h-11' : 'h-7',
            error ? 'border-destructive' : 'border-border',
          )}
        >
          {choice.map((c) => (
            <option key={c} value={c}>
              {CHOICE_LABEL[c] ?? c}
            </option>
          ))}
        </select>
      ) : (
        <input
          value={value}
          inputMode={touch ? 'none' : 'decimal'}
          readOnly={touch}
          spellCheck={false}
          autoComplete="off"
          onFocus={onFocus}
          onChange={(e) => onChange(e.target.value)}
          aria-invalid={error !== undefined}
          className={cn(
            'w-full min-w-0 rounded-sm border bg-background px-1.5 font-mono text-xs font-bold tabular-nums outline-none focus:border-primary',
            touch ? 'min-h-11' : 'h-7',
            error ? 'border-destructive' : 'border-border',
          )}
        />
      )}
      {error ? (
        <span className="text-[10px] leading-tight text-destructive">{error}</span>
      ) : (
        <span className="text-[10px] leading-tight text-muted-foreground">{field.hint}</span>
      )}
    </label>
  )
}
