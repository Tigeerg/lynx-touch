// One row of the operation form (PLAN-QUICKPROG, 7.4, C5 and 7.6): the caption with its unit, the
// input, and the reason of a failed check under the number it talks about. Three shapes of input:
// a list (`CHOICES`), a multi-line box (`MULTILINE`: a block of code, a list of points), a number.
import { cn } from '@/lib/utils'
import { CHOICE_LABEL, CHOICES, MULTILINE } from './cam-fields'

export function FieldRow({
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
    </label>
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
