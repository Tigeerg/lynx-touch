import { Check } from 'lucide-react'
import { PROBE_STEPS } from '@/lib/cnc/probe'
import { cn } from '@/lib/utils'

const TITLES = ['Выбор цикла', 'Подготовка щупа', 'Подвод к заготовке', 'Измерение', 'Результат']

// Step progress of the wizard: done steps are ticked, the active one is highlighted.
export function Stepper({ step }: { step: number }) {
  return (
    <ol className="flex shrink-0 items-center gap-0 border-b bg-muted/60 px-4 py-1.5" aria-label="Шаги измерения">
      {PROBE_STEPS.map((label, i) => {
        const done = i < step
        const active = i === step
        const last = i === PROBE_STEPS.length - 1
        return (
          <li
            key={label}
            aria-current={active ? 'step' : undefined}
            className={cn('flex items-center gap-2', !last && 'flex-1')}
          >
            <span
              className={cn(
                'flex size-6 shrink-0 items-center justify-center rounded-full border-2 font-mono text-xs font-bold transition-colors',
                done && 'border-success bg-success text-primary-foreground',
                active && 'border-primary bg-primary text-primary-foreground ring-2 ring-primary/25',
                !done && !active && 'border-border bg-card text-muted-foreground',
              )}
            >
              {done ? <Check className="size-3.5" aria-hidden="true" /> : i + 1}
            </span>
            <span
              className={cn(
                'text-[13px] font-semibold',
                active ? 'text-foreground' : done ? 'text-foreground/70' : 'text-muted-foreground',
              )}
            >
              {label}
            </span>
            {!last && (
              <span
                aria-hidden="true"
                className={cn('mx-2 h-0.5 flex-1 rounded-full', done ? 'bg-success' : 'bg-border')}
              />
            )}
          </li>
        )
      })}
    </ol>
  )
}
