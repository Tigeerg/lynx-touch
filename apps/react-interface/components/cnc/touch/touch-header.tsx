import { CircleHelp, TriangleAlert } from 'lucide-react'
import { MACHINE_NAME, RUN_LABEL, WORKPLACE_LABEL } from '@/lib/cnc/data'
import { currentFile, type Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { RUN_STYLE, WORKPLACE_ICON } from '../header-bar'
import { Clock } from '../clock'

export function TouchHeader({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const activeAlarms = state.alarms.filter((a) => a.active)
  const hasError = activeAlarms.some((a) => a.level === 'error')
  const ModeIcon = WORKPLACE_ICON[state.workplace]
  const file = currentFile(state)

  return (
    <header className="flex h-14 shrink-0 items-stretch gap-1.5 whitespace-nowrap">
      <div className="flex shrink-0 items-center gap-2 rounded-md border bg-card px-2">
        <div className="flex size-8 items-center justify-center rounded-sm bg-primary font-mono text-xs font-bold text-primary-foreground">
          ЧПУ
        </div>
        <div className="leading-tight">
          <p className="text-sm font-bold">{MACHINE_NAME}</p>
          <p className="flex items-center gap-1 font-mono text-xs font-bold text-primary">
            <ModeIcon className="size-3.5" aria-hidden="true" />
            {WORKPLACE_LABEL[state.workplace]}
          </p>
        </div>
      </div>

      <div
        className={cn(
          'flex w-24 shrink-0 items-center justify-center rounded-md border-2 px-1 text-center font-mono text-sm font-bold leading-tight',
          RUN_STYLE[state.run],
        )}
        role="status"
        aria-live="polite"
      >
        {RUN_LABEL[state.run]}
      </div>

      <div className="flex min-w-0 flex-1 items-center rounded-md border bg-card px-2.5">
        <div className="min-w-0 leading-tight">
          <p className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">Программа</p>
          <p className="truncate font-mono text-base font-bold">
            {file.name} <span className="font-sans text-xs font-medium text-muted-foreground">{file.title}</span>
          </p>
        </div>
      </div>

      <button
        type="button"
        tabIndex={-1}
        onClick={() => dispatch({ type: 'SET_SCREEN', screen: 'alarm' })}
        className={cn(
          'flex w-16 shrink-0 items-center justify-center gap-1.5 rounded-md border px-2 active:scale-[0.97]',
          hasError
            ? 'border-destructive bg-destructive/10 text-destructive'
            : activeAlarms.length
              ? 'border-warning bg-warning/15'
              : 'bg-card',
        )}
      >
        <TriangleAlert className="size-5" aria-hidden="true" />
        <span className="font-mono text-lg font-bold">{activeAlarms.length}</span>
        <span className="sr-only">активных сообщений — открыть</span>
      </button>

      <button
        type="button"
        tabIndex={-1}
        onClick={() => dispatch({ type: 'HELP' })}
        aria-label="Справка"
        className="flex w-14 shrink-0 items-center justify-center rounded-md border bg-card active:scale-[0.97] active:bg-accent"
      >
        <CircleHelp className="size-6 text-muted-foreground" aria-hidden="true" />
      </button>

      <div className="flex w-[104px] shrink-0 items-center justify-center rounded-md border bg-card">
        <Clock className="font-mono text-lg font-bold tabular-nums" />
      </div>
    </header>
  )
}
