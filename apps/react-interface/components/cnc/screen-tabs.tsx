import { Crosshair, Move, Settings, TriangleAlert, Wrench, type LucideIcon } from 'lucide-react'
import { TAB_LABEL, TABS, screenOfTab, tabOfScreen, type Screen, type Tab, type Workplace } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap } from './primitives'

export const TAB_ICON: Record<Tab, LucideIcon> = {
  work: Move,
  offset: Crosshair,
  tool: Wrench,
  alarm: TriangleAlert,
  service: Settings,
}

export function ScreenTabs({
  screen,
  workplace,
  onSelect,
  alarmCount,
  locked = [],
  lockedTitle = 'Недоступно в текущем рабочем месте',
}: {
  screen: Screen
  workplace: Workplace
  onSelect: (s: Screen) => void
  alarmCount: number
  locked?: Screen[]
  lockedTitle?: string
}) {
  const active = tabOfScreen(screen)
  return (
    <nav aria-label="Экраны" className="grid shrink-0 grid-cols-5 gap-1.5">
      {TABS.map((tab, i) => {
        const Icon = TAB_ICON[tab]
        const target = screenOfTab(tab, workplace)
        const isLocked = locked.includes(target)
        return (
          <button
            key={tab}
            type="button"
            tabIndex={-1}
            disabled={isLocked}
            title={isLocked ? lockedTitle : undefined}
            aria-disabled={isLocked || undefined}
            onClick={() => onSelect(target)}
            aria-current={tab === active ? 'page' : undefined}
            className={cn(
              'flex h-9 items-center gap-1 rounded-md border px-1.5 text-left text-xs font-semibold transition-colors',
              tab === active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card hover:bg-accent',
              isLocked && 'cursor-not-allowed opacity-40 line-through hover:bg-card',
            )}
          >
            <Keycap
              className={cn(
                'h-5 min-w-5 px-1 text-[10px]',
                tab === active && 'border-primary-foreground/40 bg-primary-foreground/15 text-primary-foreground',
              )}
            >
              {i + 1}
            </Keycap>
            <Icon className="size-4 shrink-0" aria-hidden="true" />
            <span className="truncate">{TAB_LABEL[tab]}</span>
            {tab === 'alarm' && alarmCount > 0 && (
              <span
                className={cn(
                  'ml-auto rounded-sm px-1.5 font-mono text-xs',
                  tab === active ? 'bg-primary-foreground text-primary' : 'bg-warning text-warning-foreground',
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
