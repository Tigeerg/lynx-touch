// One row of the operation form (PLAN-QUICKPROG, 7.4, C5 and 7.6): the caption with its unit, the
// input, and the reason of a failed check under the number it talks about. Four shapes of input:
// the sources of a rest (`source`, a list of operations), a list (`CHOICES`), a multi-line box
// (`MULTILINE`: a block of code, a list of points) and a number.
import { cn } from '@/lib/utils'
import type { Operation } from '@/lib/cnc/cam/ops'
import { KIND_LABEL } from './cam-labels'
import { CHOICE_LABEL, CHOICES, MULTILINE } from './cam-fields'
import { pickedSources, sourceOptions, toggleSource } from './source-choice'

export function FieldRow({
  field,
  value,
  error,
  touch,
  active,
  operations,
  onFocus,
  onChange,
}: {
  field: { key: string; label: string; unit: string; hint: string }
  value: string
  error?: string
  touch: boolean
  active: boolean
  /** The document, so `source` offers the operations a rest can take its pockets from. */
  operations: Operation[]
  onFocus: () => void
  onChange: (value: string) => void
}) {
  const choice = CHOICES[field.key]
  const sources = field.key === 'source' ? sourceOptions(operations) : null
  return (
    <div className={cn('flex flex-col gap-0.5 rounded-sm px-1 py-0.5', active && 'bg-accent/60')}>
      <span className="flex items-baseline justify-between gap-1">
        <span className="text-[10px] font-semibold text-muted-foreground">
          {field.label}
          {field.unit && <span className="ml-1 font-normal">· {field.unit}</span>}
        </span>
      </span>
      {sources !== null ? (
        <SourceList
          options={sources}
          value={value}
          touch={touch}
          onFocus={onFocus}
          onChange={onChange}
          label={field.label}
        />
      ) : choice ? (
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
      ) : MULTILINE.includes(field.key) ? (
        <MultilineField value={value} error={error} touch={touch} onFocus={onFocus} onChange={onChange} />
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
    </div>
  )
}

/**
 * The sources of a rest: one row per pocket operation of the document. The press toggles the
 * index of the row and sends `CAM_PARAM` with the text `0 2`, the format the `.qprog` carries;
 * the dialog holds no list of its own. A document without a pocket names the empty list, so the
 * operator sees why there is nothing to pick.
 */
function SourceList({
  options,
  value,
  touch,
  label,
  onFocus,
  onChange,
}: {
  options: ReturnType<typeof sourceOptions>
  value: string
  touch: boolean
  label: string
  onFocus: () => void
  onChange: (value: string) => void
}) {
  const picked = pickedSources(value)
  if (options.length === 0) {
    return (
      <p className="rounded-sm border border-dashed px-1.5 py-1 text-[10px] leading-tight text-muted-foreground">
        В программе нет карманов: остаток пойдёт по всей площади.
      </p>
    )
  }
  return (
    <div role="group" aria-label={label} className="grid grid-cols-2 gap-1">
      {options.map((option) => {
        const on = picked.includes(option.index)
        return (
          <button
            key={option.index}
            type="button"
            tabIndex={-1}
            aria-pressed={on}
            onFocus={onFocus}
            onClick={() => onChange(toggleSource(value, option.index))}
            className={cn(
              'flex min-w-0 items-center gap-1 rounded-sm border px-1 text-left text-[10px] font-semibold transition-colors hover:bg-accent',
              touch ? 'min-h-11' : 'min-h-7',
              on ? 'border-primary bg-primary/10' : 'border-border bg-background',
            )}
          >
            <span className="shrink-0 font-mono tabular-nums text-muted-foreground">{option.number}.</span>
            <span className="min-w-0 truncate">{KIND_LABEL[option.kind]}</span>
          </button>
        )
      })}
    </div>
  )
}

/** The multi-line box of a block of code or a list of points: one line of text per line, mono font,
 * a fixed height so the form keeps its rows on 1024x600. The touch variant has no keyboard, so the
 * text is read only; a typed block is a panel job. */
function MultilineField({
  value,
  error,
  touch,
  onFocus,
  onChange,
}: {
  value: string
  error?: string
  touch: boolean
  onFocus: () => void
  onChange: (value: string) => void
}) {
  return (
    <textarea
      value={value}
      rows={4}
      readOnly={touch}
      spellCheck={false}
      autoComplete="off"
      onFocus={onFocus}
      onChange={(e) => onChange(e.target.value)}
      aria-invalid={error !== undefined}
      className={cn(
        'w-full min-w-0 resize-none rounded-sm border bg-background px-1.5 py-1 font-mono text-xs font-bold leading-tight outline-none focus:border-primary',
        error ? 'border-destructive' : 'border-border',
      )}
    />
  )
}
