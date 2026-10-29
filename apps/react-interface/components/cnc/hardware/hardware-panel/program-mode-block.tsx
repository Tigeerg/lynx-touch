'use client'

import { CirclePause, FastForward, House, Slash, StepForward, type LucideIcon } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { HwGroup, HwKey, KEY_ROW, SMALL_KEY } from '../hw-parts'

type Dispatch = (a: Action) => void

export function ProgramModeBlock({ state, dispatch }: { state: MachineState; dispatch: Dispatch }) {
  const keys: {
    key: 'singleBlock' | 'optStop' | 'dryRun' | 'blockSkip'
    label: string
    title: string
    icon: LucideIcon
    tone: 'warning' | 'primary'
  }[] = [
    { key: 'singleBlock', label: 'Покадр.', title: 'Покадровая отработка', icon: StepForward, tone: 'warning' },
    { key: 'optStop', label: 'М01', title: 'Опциональный останов', icon: CirclePause, tone: 'warning' },
    { key: 'dryRun', label: 'Холост.', title: 'Холостой ход', icon: FastForward, tone: 'warning' },
    { key: 'blockSkip', label: 'Проп. /', title: 'Пропуск кадров, начинающихся с «/»', icon: Slash, tone: 'warning' },
  ]
  return (
    <div role="group" aria-label="Режим отработки" className={KEY_ROW}>
      <div className="flex justify-center gap-2">
        {keys.map((t) => (
          <HwKey
            key={t.key}
            label={t.label}
            icon={t.icon}
            title={t.title}
            lit={state[t.key]}
            tone={t.tone}
            onClick={() => dispatch({ type: 'TOGGLE', key: t.key })}
            className={SMALL_KEY}
          />
        ))}
        <HwKey
          label="Безоп. т."
          icon={House}
          title="Уйти в безопасную точку: Z вверх, затем X0 Y0"
          lit={state.homing}
          tone="primary"
          onClick={() => dispatch({ type: 'GO_SAFE' })}
          className={SMALL_KEY}
        />
      </div>
    </div>
  )
}
