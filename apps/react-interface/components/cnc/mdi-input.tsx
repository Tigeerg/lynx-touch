'use client'

import { CornerDownLeft, Delete, Keyboard, Play } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

const EXAMPLES = ['G0 X0 Y0', 'G0 Z50', 'S1500 M3', 'M5', 'G1 X10 F300', 'G28']

export function MdiInput({
  state,
  dispatch,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  className?: string
}) {
  const { buffer, history, running } = state.mdi
  const busy = state.run === 'RUN' || state.run === 'HOLD'

  return (
    <section aria-label="Командная строка MDI" className={cn('flex min-h-0 flex-col gap-1.5', className)}>
      <div
        className={cn(
          'flex shrink-0 flex-col gap-1.5 rounded-md border-2 bg-card p-2.5',
          running ? 'border-success' : busy ? 'border-warning' : 'border-primary',
        )}
      >
        <div className="flex items-center justify-between text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
          <span className="flex items-center gap-1.5">
            <Keyboard className="size-3.5" aria-hidden="true" />
            Ввод с клавиатуры стойки
          </span>
          <span role="status" className={cn(running ? 'text-success' : busy && 'text-warning-foreground')}>
            {running ? 'Выполняется' : busy ? 'Станок занят' : 'Ожидание кадра'}
          </span>
        </div>

        <div className="flex items-center gap-3 rounded-sm bg-muted px-3 py-2" aria-live="polite">
          <span className="shrink-0 font-mono text-xl font-bold text-primary" aria-hidden="true">
            MDI&gt;
          </span>
          <output className="flex min-w-0 flex-1 items-center font-mono text-3xl font-bold uppercase tracking-wide text-foreground">
            <span className="truncate whitespace-pre">{buffer}</span>
            {!busy && (
              <span className="animate-caret ml-0.5 inline-block h-8 w-4 shrink-0 bg-primary" aria-hidden="true" />
            )}
            {!buffer && busy && (
              <span className="truncate text-muted-foreground">{running ? (history[0] ?? '') : ''}</span>
            )}
          </output>
        </div>

        <div className="flex items-center justify-between gap-2">
          <div className="flex flex-wrap gap-1">
            {EXAMPLES.map((ex) => (
              <button
                key={ex}
                type="button"
                tabIndex={-1}
                disabled={busy}
                onClick={() => dispatch({ type: 'MDI_BUFFER', value: ex })}
                className="rounded-sm border bg-background px-1.5 py-0.5 font-mono text-xs text-muted-foreground transition-colors hover:bg-accent hover:text-foreground disabled:opacity-40"
              >
                {ex}
              </button>
            ))}
          </div>
          <div className="flex shrink-0 items-center gap-1.5">
            <button
              type="button"
              tabIndex={-1}
              disabled={busy || !buffer}
              onClick={() => dispatch({ type: 'MDI_BUFFER', value: '' })}
              className="flex h-8 items-center gap-1.5 rounded-md border bg-background px-2.5 text-xs font-semibold transition-colors hover:bg-accent disabled:opacity-40"
            >
              <Delete className="size-4" aria-hidden="true" />
              Очистить
            </button>
            <button
              type="button"
              tabIndex={-1}
              disabled={busy || !buffer.trim()}
              onClick={() => dispatch({ type: 'CYCLE_START' })}
              className="flex h-8 items-center gap-1.5 rounded-md bg-primary px-3 text-xs font-semibold text-primary-foreground transition-opacity hover:opacity-90 disabled:opacity-40"
            >
              <Play className="size-4" aria-hidden="true" />
              Пуск
              <CornerDownLeft className="size-3.5 opacity-70" aria-hidden="true" />
            </button>
          </div>
        </div>
      </div>
    </section>
  )
}
