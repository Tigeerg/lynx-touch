'use client'

import { WORKPLACE_LABEL } from '@/lib/cnc/data'
import type { Action } from '@/lib/cnc/machine'
import { WORKPLACES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { WORKPLACE_ICON } from '../../header-bar'
import { Panel } from '../../primitives'
import { KEY_BASE } from './hold-button'

export function WorkplaceColumn({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  return (
    <Panel title="Режим" className="w-[150px] shrink-0">
      <div className="grid h-full grid-rows-3 gap-1.5 p-1.5" role="radiogroup" aria-label="Режим">
        {WORKPLACES.map((w) => {
          const Icon = WORKPLACE_ICON[w]
          const active = w === state.workplace
          return (
            <button
              key={w}
              type="button"
              role="radio"
              aria-checked={active}
              tabIndex={-1}
              onClick={() => dispatch({ type: 'SET_WORKPLACE', workplace: w })}
              className={cn(
                KEY_BASE,
                'justify-center gap-1.5 px-1 text-center text-sm whitespace-nowrap',
                active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card active:bg-accent',
              )}
            >
              <Icon className="size-5 shrink-0" aria-hidden="true" />
              <span>{WORKPLACE_LABEL[w]}</span>
            </button>
          )
        })}
      </div>
    </Panel>
  )
}
