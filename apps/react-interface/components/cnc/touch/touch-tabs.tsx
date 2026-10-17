import { Lock } from 'lucide-react'
import { TAB_LABEL, TABS, screenOfTab, tabOfScreen, type Screen, type Workplace } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { TAB_ICON } from '../screen-tabs'

export function TouchTabs({
  screen,
  workplace,
  onSelect,
  alarmCount,
  locked,
}: {
  screen: Screen
  workplace: Workplace
  onSelect: (s: Screen) => void
  alarmCount: number
  locked: Screen[]
}) {
  const active = tabOfScreen(screen)
  return (
    <nav aria-label="Экраны" className="grid h-16 shrink-0 grid-cols-5 gap-1.5">
      {TABS.map((tab) => {
        const Icon = TAB_ICON[tab]
        const target = screenOfTab(tab, workplace)
        const isLocked = locked.includes(target)
        return (
          <button
            key={tab}
            type="button"
            tabIndex={-1}
            disabled={isLocked}
            onClick={() => onSelect(target)}
            aria-current={tab === active ? 'page' : undefined}
            className={cn(
              'relative flex flex-col items-center justify-center gap-1 rounded-md border text-xs font-semibold transition-colors active:scale-[0.97]',
              tab === active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card active:bg-accent',
              isLocked && 'opacity-40',
            )}
          >
            {isLocked ? <Lock className="size-5" aria-hidden="true" /> : <Icon className="size-5" aria-hidden="true" />}
            <span className="max-w-full truncate px-1">{TAB_LABEL[tab]}</span>
            {isLocked && <span className="sr-only">(недоступно в текущем режиме)</span>}
            {tab === 'alarm' && alarmCount > 0 && (
              <span
                className={cn(
                  'absolute right-1 top-1 min-w-5 rounded-sm px-1 font-mono text-xs leading-5',
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
