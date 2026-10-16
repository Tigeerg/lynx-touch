import { Disc3, Hand, Repeat, SquareTerminal, TriangleAlert, type LucideIcon } from 'lucide-react'
import { MACHINE_NAME, MODE_LABEL, RUN_LABEL } from '@/lib/cnc/data'
import { currentFile } from '@/lib/cnc/machine'
import { MODES, type MachineState, type Mode } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

export const MODE_ICON: Record<Mode, LucideIcon> = {
  AUTO: Repeat,
  MDI: SquareTerminal,
  JOG: Hand,
  MPG: Disc3,
}

export const RUN_STYLE: Record<MachineState['run'], string> = {
  IDLE: 'bg-card text-foreground border-foreground/20',
  RUN: 'bg-success text-white border-success',
  HOLD: 'bg-warning text-warning-foreground border-warning',
  ALARM: 'bg-destructive text-white border-destructive',
  ESTOP: 'bg-destructive text-white border-destructive animate-pulse',
}

export function HeaderBar({
  state,
  clock,
  focused,
  hardwareOpen,
  onToggleHardware,
}: {
  state: MachineState
  clock: string | null
  focused: boolean
  hardwareOpen: boolean
  onToggleHardware: () => void
}) {
  const activeAlarms = state.alarms.filter((a) => a.active)
  const hasError = activeAlarms.some((a) => a.level === 'error')
  const ModeIcon = MODE_ICON[state.mode]

  return (
    <header className="flex h-11 shrink-0 items-stretch gap-1.5 whitespace-nowrap">
      <div className="flex shrink-0 items-center gap-2 rounded-md border bg-card px-2">
        <div className="flex size-7 items-center justify-center rounded-sm bg-primary font-mono text-[11px] font-bold text-primary-foreground">
          ЧПУ
        </div>
        <p className="text-sm font-bold">{MACHINE_NAME}</p>
      </div>

      <div className="flex shrink-0 items-center gap-2 rounded-md border bg-card px-2">
        <div className="flex items-center gap-1.5">
          <ModeIcon className="size-5 text-primary" aria-hidden="true" />
          <div className="leading-none">
            <p className="text-[9px] font-semibold uppercase tracking-wider text-muted-foreground">Режим</p>
            <p className="w-20 whitespace-nowrap font-mono text-base font-bold">{MODE_LABEL[state.mode]}</p>
          </div>
        </div>
        <ol className="flex items-center gap-0.5" aria-label="Переключатель режимов">
          {MODES.map((m) => {
            const Icon = MODE_ICON[m]
            const active = m === state.mode
            return (
              <li
                key={m}
                title={MODE_LABEL[m]}
                aria-current={active ? 'true' : undefined}
                className={cn(
                  'flex size-7 items-center justify-center rounded-sm border',
                  active ? 'border-primary bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
                )}
              >
                <Icon className="size-3.5" aria-hidden="true" />
                <span className="sr-only">{MODE_LABEL[m]}</span>
              </li>
            )
          })}
        </ol>
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

      <div className="flex min-w-0 flex-1 items-center rounded-md border bg-card px-2">
        <div className="min-w-0 leading-none">
          <p className="text-[9px] font-semibold uppercase tracking-wider text-muted-foreground">Программа</p>
          <p className="truncate font-mono text-sm font-bold">
            {currentFile(state).name}{' '}
            <span className="font-sans text-xs font-medium text-muted-foreground">{currentFile(state).title}</span>
          </p>
        </div>
      </div>

      <div
        className={cn(
          'flex shrink-0 items-center gap-1.5 rounded-md border px-2',
          hasError
            ? 'border-destructive bg-destructive/10 text-destructive'
            : activeAlarms.length
              ? 'border-warning bg-warning/15'
              : 'bg-card',
        )}
      >
        <TriangleAlert className="size-4" aria-hidden="true" />
        <span className="font-mono text-base font-bold">{activeAlarms.length}</span>
        <span className="sr-only">активных сообщений</span>
      </div>

      <div className="flex shrink-0 items-center gap-2 rounded-md border bg-card px-2">
        <button
          type="button"
          tabIndex={-1}
          aria-pressed={hardwareOpen}
          onMouseDown={(e) => e.preventDefault()}
          onClick={onToggleHardware}
          title="Показать или скрыть физический пульт"
          className={cn(
            'flex h-7 items-center rounded-sm border px-2 text-[11px] font-semibold transition-colors',
            hardwareOpen ? 'border-primary bg-primary text-primary-foreground' : 'bg-muted hover:bg-accent',
          )}
        >
          Пульт
        </button>
        <span
          role="img"
          aria-label={focused ? 'Пульт принимает нажатия клавиш' : 'Нет фокуса'}
          title={focused ? 'Пульт принимает нажатия клавиш' : 'Кликните по экрану, чтобы активировать клавиатуру'}
          className={cn('size-2 rounded-full', focused ? 'bg-success' : 'bg-muted-foreground/50')}
        />
        <time className="font-mono text-base font-bold tabular-nums" suppressHydrationWarning>
          {clock ?? '--:--:--'}
        </time>
      </div>
    </header>
  )
}
