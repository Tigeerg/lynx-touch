'use client'

import { Lock, LockOpen, RotateCcw, RotateCw, Square } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { HwKey, KEY_ROW, SMALL_KEY } from '../hw-parts'

type Dispatch = (a: Action) => void

export function SpindleBlock({ state, dispatch }: { state: MachineState; dispatch: Dispatch }) {
  const cw = state.spindleOn && state.spindleDir === 1
  const ccw = state.spindleOn && state.spindleDir === -1
  return (
    <div role="group" aria-label="Инструмент и шпиндель" className={KEY_ROW}>
      <div className="flex justify-center gap-2">
        <HwKey
          label="Зажим"
          icon={Lock}
          title="Зажать инструмент"
          lit={state.toolClamped}
          tone="success"
          onClick={() => dispatch({ type: 'TOOL_CLAMP', clamp: true })}
          className={SMALL_KEY}
        />
        <HwKey
          label="Разжим"
          icon={LockOpen}
          title="Разжать инструмент"
          lit={!state.toolClamped}
          tone="warning"
          onClick={() => dispatch({ type: 'TOOL_CLAMP', clamp: false })}
          className={SMALL_KEY}
        />
        <HwKey
          label="M3"
          icon={RotateCw}
          title="Шпиндель по часовой"
          lit={cw}
          tone="success"
          onClick={() => dispatch({ type: 'SPINDLE_MANUAL', dir: 1 })}
          className={SMALL_KEY}
        />
        <HwKey
          label="M4"
          icon={RotateCcw}
          title="Шпиндель против часовой"
          lit={ccw}
          tone="success"
          onClick={() => dispatch({ type: 'SPINDLE_MANUAL', dir: -1 })}
          className={SMALL_KEY}
        />
        <HwKey
          label="M5"
          icon={Square}
          title="Остановить шпиндель"
          lit={!state.spindleOn}
          tone="warning"
          onClick={() => dispatch({ type: 'SPINDLE_STOP' })}
          className={SMALL_KEY}
        />
      </div>
    </div>
  )
}
