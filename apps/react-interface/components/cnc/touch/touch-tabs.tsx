import { TAB_LABEL, TABS, screenOfTab, tabOfScreen, type Screen, type Workplace } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { TAB_ICON } from '../screen-tabs'

export function TouchTabs({
  screen,
  workplace,
  onSelect,
  alarmCount,
}: {
  screen: Screen
  workplace: Workplace
  onSelect: (s: Screen) => void
  alarmCount: number
}) {
  const active = tabOfScreen(screen)
  return (
    <nav aria-label="Экраны" className="grid h-10 shrink-0 grid-cols-5 gap-[var(--panel-gap)]">
      {TABS.map((tab) => {
        const Icon = TAB_ICON[tab]
        const target = screenOfTab(tab, workplace)
        return (
          <button
            key={tab}
            type="button"
            tabIndex={-1}
            onClick={() => onSelect(target)}
            aria-current={tab === active ? 'page' : undefined}
            className={cn(
              'relative flex flex-row items-center justify-center gap-1.5 rounded-md border px-1 text-[15px] font-semibold leading-none transition-colors active:scale-[0.97]',
              tab === active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card active:bg-accent',
            )}
          >
            <Icon className="size-4 shrink-0" aria-hidden="true" />
            <span className="whitespace-nowrap">{TAB_LABEL[tab]}</span>
            {tab === 'alarm' && alarmCount > 0 && (
              <span
                className={cn(
                  'absolute right-1 top-0.5 min-w-5 rounded-sm px-1 text-center font-mono text-xs leading-4',
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
