import { Disc3, Hand } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import { JOG_FEEDS, MPG_STEPS, mpgMultiplier, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Section } from './sections'

export function JogSection({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  return (
    <Section
      icon={Hand}
      title="Ручная подача"
      right={
        <span className="font-mono font-bold">
          Ось {state.jogAxis} · шаг {state.jogStep}
        </span>
      }
    >
      <div role="radiogroup" aria-label="Скорость ручной подачи, мм/мин" className="grid grid-cols-4 gap-1">
        {JOG_FEEDS.map((f) => (
          <button
            key={f}
            type="button"
            tabIndex={-1}
            role="radio"
            aria-checked={state.jogFeed === f}
            onClick={() => dispatch({ type: 'SET_JOG_FEED', feed: f })}
            className={cn(
              'h-7 rounded-sm border font-mono text-xs font-bold tabular-nums transition-colors',
              state.jogFeed === f
                ? 'border-primary bg-primary text-primary-foreground'
                : 'bg-background hover:bg-accent',
            )}
          >
            {f}
          </button>
        ))}
      </div>
    </Section>
  )
}

export function MpgSection({ state }: { state: MachineState }) {
  return (
    <Section icon={Disc3} title="Маховик" right={<span className="font-mono font-bold">Ось {state.jogAxis}</span>}>
      <div role="status" aria-label="Множитель шага маховика" className="grid grid-cols-3 gap-1">
        {MPG_STEPS.map((step) => {
          const active = state.jogStep === step
          return (
            <div
              key={step}
              className={cn(
                'flex flex-col items-center rounded-sm border py-0.5 transition-colors',
                active ? 'border-primary bg-primary text-primary-foreground' : 'bg-background text-muted-foreground',
              )}
            >
              <span className="font-mono text-lg font-bold leading-tight tabular-nums">×{mpgMultiplier(step)}</span>
              <span className="font-mono text-[10px] font-semibold leading-tight">{step} мм</span>
            </div>
          )
        })}
      </div>
    </Section>
  )
}
