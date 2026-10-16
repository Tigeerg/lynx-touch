import { FileCode, Move, Crosshair, Wrench, TriangleAlert, Settings, type LucideIcon } from 'lucide-react'
import { SCREEN_LABEL } from '@/lib/cnc/data'
import { SCREENS, type Screen } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap } from './primitives'

export const SCREEN_ICON: Record<Screen, LucideIcon> = {
  pos: Move,
  prog: FileCode,
  offset: Crosshair,
  tool: Wrench,
  alarm: TriangleAlert,
  service: Settings,
}

export function ScreenTabs({
  screen,
  onSelect,
  alarmCount,
  locked = [],
  lockedTitle = 'Недоступно в текущем режиме',
}: {
  screen: Screen
  onSelect: (s: Screen) => void
  alarmCount: number
  locked?: Screen[]
  lockedTitle?: string
}) {
  return (
    <nav aria-label="Экраны" className="grid shrink-0 grid-cols-6 gap-1.5">
      {SCREENS.map((s, i) => {
        const Icon = SCREEN_ICON[s]
        const active = s === screen
        const isLocked = locked.includes(s)
        return (
          <button
            key={s}
            type="button"
            tabIndex={-1}
            disabled={isLocked}
            title={isLocked ? lockedTitle : undefined}
            aria-disabled={isLocked || undefined}
            onClick={() => onSelect(s)}
            aria-current={active ? 'page' : undefined}
            className={cn(
              'flex h-9 items-center gap-1 rounded-md border px-1.5 text-left text-xs font-semibold transition-colors',
              active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card hover:bg-accent',
              isLocked && 'cursor-not-allowed opacity-40 line-through hover:bg-card',
            )}
          >
            <Keycap
              className={cn(
                'h-5 min-w-5 px-1 text-[10px]',
                active && 'border-primary-foreground/40 bg-primary-foreground/15 text-primary-foreground',
              )}
            >
              {i + 1}
            </Keycap>
            <Icon className="size-4 shrink-0" aria-hidden="true" />
            <span className="truncate">{SCREEN_LABEL[s]}</span>
            {s === 'alarm' && alarmCount > 0 && (
              <span
                className={cn(
                  'ml-auto rounded-sm px-1.5 font-mono text-xs',
                  active ? 'bg-primary-foreground text-primary' : 'bg-warning text-warning-foreground',
                )}
              >
                {alarmCount}
              </span>
            )}
          </button>
        )
      })}
    </nav>
  )
}
