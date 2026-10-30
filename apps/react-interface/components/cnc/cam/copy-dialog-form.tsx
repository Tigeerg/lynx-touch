// The box both copying dialogs share (PLAN-QUICKPROG, 7.6): a caption, one row per number, a refusal
// under the field it names and one «Создать копии» button. `copy-dialogs.tsx` decides which numbers
// are sent; this file only draws them and keeps the refusal the dialog already knows.
'use client'

import { useId, useState } from 'react'
import { CopyPlus, X } from 'lucide-react'
import { cn } from '@/lib/utils'
import { useUiMode } from '../ui-mode-context'

/** The shell both copying dialogs share: a caption, the number rows and one «Создать» button. */
export function CopyDialog({
  title,
  icon: Icon,
  fields,
  text,
  onChange,
  onSend,
  onClose,
}: {
  title: string
  icon: typeof CopyPlus
  fields: { key: string; label: string; unit: string; hint: string }[]
  text: Record<string, string>
  onChange: (key: string, value: string) => void
  /** Send the numbers; a refusal keeps the dialog open and names the field. */
  onSend: () => { ok: boolean; problem?: { key: string; reason: string } }
  onClose: () => void
}) {
  const touch = useUiMode()?.mode === 'touch'
  const titleId = useId()
  const [problem, setProblem] = useState<{ key: string; reason: string } | null>(null)

  const send = () => {
    const result = onSend()
    if (result.ok && result.problem === undefined) return onClose()
    setProblem(result.problem ?? { key: 'cols', reason: 'Копии не созданы: проверьте числа.' })
  }

  return (
    <div className="absolute inset-0 z-50 flex items-center justify-center bg-foreground/30 p-3">
      <section
        role="dialog"
        aria-modal="true"
        aria-labelledby={titleId}
        className="flex w-72 flex-col gap-1.5 rounded-md border bg-card p-2 shadow-xl"
      >
        <header className="flex items-center gap-1.5">
          <Icon className="size-4 shrink-0 text-primary" aria-hidden="true" />
          <h3 id={titleId} className="min-w-0 flex-1 truncate text-xs font-bold">
            {title}
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
        {fields.map((field) => (
          <label key={field.key} className="flex flex-col gap-0.5">
            <span className="text-[10px] font-semibold text-muted-foreground">
              {field.label}
              {field.unit && <span className="ml-1 font-normal">· {field.unit}</span>}
            </span>
            <input
              value={text[field.key] ?? ''}
              inputMode={touch ? 'none' : 'decimal'}
              readOnly={touch}
              spellCheck={false}
              autoComplete="off"
              onChange={(e) => onChange(field.key, e.target.value)}
              aria-invalid={problem?.key === field.key}
              className={cn(
                'w-full min-w-0 rounded-sm border bg-background px-1.5 font-mono text-xs font-bold tabular-nums outline-none focus:border-primary',
                touch ? 'min-h-11' : 'h-7',
                problem?.key === field.key ? 'border-destructive' : 'border-border',
              )}
            />
          </label>
        ))}
        {problem ? (
          <p className="text-[10px] leading-tight text-destructive" role="status">
            {problem.reason}
          </p>
        ) : (
          <p className="text-[10px] leading-tight text-muted-foreground">
            Копии встанут в список за исходной операцией.
          </p>
        )}
        <div className="mt-0.5 flex gap-1.5">
          <button
            type="button"
            tabIndex={-1}
            onClick={send}
            className={cn(
              'flex flex-1 items-center justify-center gap-1.5 rounded-md border-2 border-primary bg-primary text-xs font-bold text-primary-foreground hover:opacity-90',
              touch ? 'min-h-12' : 'min-h-9',
            )}
          >
            <CopyPlus className="size-4" aria-hidden="true" />
            Создать копии
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
