import { FlaskConical, Play, TriangleAlert, Wrench, type LucideIcon } from 'lucide-react'
import { MACHINE_NAME, RUN_LABEL, WORKPLACE_LABEL } from '@/lib/cnc/data'
import { currentFile } from '@/lib/cnc/machine'
import { paramValue, THEME_OPTIONS, THEME_PARAM } from '@/lib/cnc/service-data'
import { WORKPLACES, type MachineState, type Workplace } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

export const WORKPLACE_ICON: Record<Workplace, LucideIcon> = {
  SETUP: Wrench,
  TEST: FlaskConical,
  RUN: Play,
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
  onToggleTheme,
  onWorkplace,
}: {
  state: MachineState
  clock: string | null
  focused: boolean
  hardwareOpen: boolean
  onToggleHardware: () => void
  onToggleTheme: () => void
  onWorkplace: (w: Workplace) => void
}) {
  const activeAlarms = state.alarms.filter((a) => a.active)
  const hasError = activeAlarms.some((a) => a.level === 'error')
  const WorkplaceIcon = WORKPLACE_ICON[state.workplace]

  return (
    <header className="flex h-11 shrink-0 items-stretch gap-[var(--panel-gap)] whitespace-nowrap">
      <div className="flex shrink-0 items-center gap-2 rounded-md border bg-card px-2">
        <div className="flex size-7 items-center justify-center rounded-sm bg-primary font-mono text-[11px] font-bold text-primary-foreground">
          ЧПУ
        </div>
        <p className="text-sm font-bold">{MACHINE_NAME}</p>
      </div>

      <div className="flex shrink-0 items-center gap-1.5 rounded-md border bg-card px-1.5">
        <div className="flex items-center gap-1.5">
          <WorkplaceIcon className="size-5 text-primary" aria-hidden="true" />
          <div className="leading-none">
            <p className="text-[9px] font-semibold uppercase tracking-wider text-muted-foreground">Режим</p>
            <p className="w-[72px] truncate whitespace-nowrap font-mono text-sm font-bold">
              {WORKPLACE_LABEL[state.workplace]}
            </p>
          </div>
        </div>
        <ol className="flex items-center gap-0.5" aria-label="Режим">
          {WORKPLACES.map((w) => {
            const Icon = WORKPLACE_ICON[w]
            const active = w === state.workplace
            return (
              <li key={w} title={WORKPLACE_LABEL[w]} aria-current={active ? 'true' : undefined}>
                <button
                  type="button"
                  tabIndex={-1}
                  aria-pressed={active}
                  onMouseDown={(e) => e.preventDefault()}
                  onClick={() => onWorkplace(w)}
                  className={cn(
                    'wp-btn flex size-7 items-center justify-center rounded-sm border transition-colors',
                    active
                      ? 'border-primary bg-primary text-primary-foreground'
                      : 'bg-muted text-muted-foreground hover:bg-accent',
                  )}
                >
                  <Icon className="size-3.5" aria-hidden="true" />
                  <span className="wp-word hidden truncate text-[10px] font-semibold">{WORKPLACE_LABEL[w]}</span>
                  <span className="sr-only">{WORKPLACE_LABEL[w]}</span>
                </button>
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
        <button
          type="button"
          tabIndex={-1}
          onMouseDown={(e) => e.preventDefault()}
          onClick={onToggleTheme}
          title={`Тема: ${THEME_OPTIONS[paramValue(state.params, THEME_PARAM)]}. Нажмите, чтобы сменить`}
          aria-label={`Сменить тему. Сейчас: ${THEME_OPTIONS[paramValue(state.params, THEME_PARAM)]}`}
          className="flex h-7 items-center rounded-sm border bg-muted px-2 text-[11px] font-semibold transition-colors hover:bg-accent"
        >
          Тема
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
