import { Lock } from 'lucide-react'
import { SCREEN_LABEL } from '@/lib/cnc/data'
import { SCREENS, type Screen } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { SCREEN_ICON } from '../screen-tabs'

export function TouchTabs({
  screen,
  onSelect,
  alarmCount,
  locked,
}: {
  screen: Screen
  onSelect: (s: Screen) => void
  alarmCount: number
  locked: Screen[]
}) {
  return (
    <nav aria-label="Экраны" className="grid h-16 shrink-0 grid-cols-6 gap-1.5">
      {SCREENS.map((s) => {
        const Icon = SCREEN_ICON[s]
        const active = s === screen
        const isLocked = locked.includes(s)
        return (
          <button
            key={s}
            type="button"
            tabIndex={-1}
            disabled={isLocked}
            onClick={() => onSelect(s)}
            aria-current={active ? 'page' : undefined}
            className={cn(
              'relative flex flex-col items-center justify-center gap-1 rounded-md border text-xs font-semibold transition-colors active:scale-[0.97]',
              active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card active:bg-accent',
              isLocked && 'opacity-40',
            )}
          >
            {isLocked ? <Lock className="size-5" aria-hidden="true" /> : <Icon className="size-5" aria-hidden="true" />}
            <span className="max-w-full truncate px-1">{SCREEN_LABEL[s]}</span>
            {isLocked && <span className="sr-only">(недоступно в текущем режиме)</span>}
            {s === 'alarm' && alarmCount > 0 && (
              <span
                className={cn(
                  'absolute right-1 top-1 min-w-5 rounded-sm px-1 font-mono text-xs leading-5',
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
