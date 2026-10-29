'use client'

import { OctagonX, Pause, Play, RotateCcw, type LucideIcon } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { KEY_BASE } from './hold-button'

type CycleItem = { label: string; icon: LucideIcon; lit: boolean; tone: string; idle: string; action: Action }

export function CycleRow({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const items: CycleItem[] = [
    {
      label: 'Пуск',
      icon: Play,
      lit: state.run === 'RUN',
      tone: 'border-hw-start bg-hw-start text-hw-ink brightness-95',
      idle: 'border-hw-start bg-hw-start text-hw-ink',
      action: { type: 'CYCLE_START' },
    },
    {
      label: 'Стоп',
      icon: Pause,
      lit: state.run === 'HOLD',
      tone: 'border-hw-stop bg-hw-stop text-hw-ink brightness-95',
      idle: 'border-hw-stop bg-hw-stop text-hw-ink',
      action: { type: 'FEED_HOLD' },
    },
    {
      label: 'Сброс',
      icon: RotateCcw,
      lit: state.run === 'ALARM',
      tone: 'border-hw-reset bg-hw-reset text-hw-ink brightness-95',
      idle: 'border-hw-reset bg-hw-reset text-hw-ink',
      action: { type: 'RESET' },
    },
    {
      label: 'Авария',
      icon: OctagonX,
      lit: state.run === 'ESTOP',
      tone: 'border-destructive bg-destructive text-white',
      idle: 'border-destructive bg-destructive/15 text-destructive',
      action: { type: 'ESTOP' },
    },
  ]
  return (
    <div className="grid h-[72px] shrink-0 grid-cols-4 gap-1.5" role="group" aria-label="Управление циклом">
      {items.map(({ label, icon: Icon, lit, tone, idle, action }) => (
        <button
          key={label}
          type="button"
          tabIndex={-1}
          aria-pressed={lit}
          onClick={() => dispatch(action)}
          className={cn(
            KEY_BASE,
            'gap-2.5 border-2 border-b-4 px-3 text-lg uppercase tracking-wide',
            lit ? tone : idle,
          )}
        >
          <Icon className="size-7" aria-hidden="true" />
          {label}
        </button>
      ))}
    </div>
  )
}
