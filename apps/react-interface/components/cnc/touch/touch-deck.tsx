'use client'

import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { CycleRow } from './touch-deck/cycle-row'
import { FunctionsPanel } from './touch-deck/functions-panel'
import { JogPanel } from './touch-deck/jog-panel'
import { WorkplaceColumn } from './touch-deck/workplace-column'

export function TouchDeck({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  return (
    <section aria-label="Сенсорный пульт" className="flex shrink-0 flex-col gap-1.5">
      <div className="flex h-[280px] gap-1.5">
        <WorkplaceColumn state={state} dispatch={dispatch} />
        <JogPanel state={state} dispatch={dispatch} />
        <FunctionsPanel state={state} dispatch={dispatch} />
      </div>
      <CycleRow state={state} dispatch={dispatch} />
    </section>
  )
}
