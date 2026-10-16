'use client'

import { CornerDownLeft, Delete, Wrench, X } from 'lucide-react'
import type { Dispatch } from 'react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Overlay } from './overlay'

const key =
  'flex h-12 items-center justify-center rounded-md border border-b-[3px] bg-card font-mono text-lg font-bold transition-[transform,background-color] hover:bg-accent active:translate-y-px active:border-b-2'

export function ToolCallDialog({ state, dispatch }: { state: MachineState; dispatch: Dispatch<Action> }) {
  return (
    <Overlay
      onClose={() => dispatch({ type: 'TOOL_DIALOG', open: false })}
      label="Быстрый вызов инструмента"
      className="w-full max-w-xs border-2 border-foreground/20"
    >
      <header className="flex items-center gap-2 border-b bg-muted px-4 py-2">
        <Wrench className="size-4 text-primary" aria-hidden="true" />
        <h2 className="text-xs font-bold uppercase tracking-wide">Вызов инструмента T-код M6</h2>
        <button
          type="button"
          tabIndex={-1}
          aria-label="Закрыть"
          className="ml-auto rounded p-1 hover:bg-accent"
          onClick={() => dispatch({ type: 'TOOL_DIALOG', open: false })}
        >
          <X className="size-4" aria-hidden="true" />
        </button>
      </header>

      <p className="border-b px-4 py-3 text-center font-mono text-4xl font-bold tabular-nums" aria-live="polite">
        T{state.toolBuffer.padEnd(2, '_')}
      </p>

      <div aria-label="Инструменты таблицы" className="flex flex-wrap justify-center gap-1 border-b px-3 py-2">
        {state.tools.map((t) => (
          <button
            key={t.number}
            type="button"
            tabIndex={-1}
            onClick={() => dispatch({ type: 'TOOL_INPUT', value: String(t.number) })}
            className="rounded-sm bg-muted px-1.5 py-0.5 font-mono text-xs font-bold tabular-nums hover:bg-accent"
          >
            T{String(t.number).padStart(2, '0')}
          </button>
        ))}
      </div>

      <div role="group" aria-label="Цифровая клавиатура" className="grid grid-cols-3 gap-1.5 p-3">
        {['1', '2', '3', '4', '5', '6', '7', '8', '9'].map((d) => (
          <button
            key={d}
            type="button"
            tabIndex={-1}
            className={key}
            onClick={() => dispatch({ type: 'TOOL_INPUT', value: d })}
          >
            {d}
          </button>
        ))}
        <button
          type="button"
          tabIndex={-1}
          aria-label="Стереть"
          className={cn(key, 'text-muted-foreground')}
          onClick={() => dispatch({ type: 'TOOL_BACK' })}
        >
          <Delete className="size-5" aria-hidden="true" />
        </button>
        <button
          type="button"
          tabIndex={-1}
          className={key}
          onClick={() => dispatch({ type: 'TOOL_INPUT', value: '0' })}
        >
          0
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-label="Вызвать"
          className={cn(key, 'border-primary bg-primary text-primary-foreground hover:bg-primary/90')}
          onClick={() => dispatch({ type: 'TOOL_RUN' })}
        >
          <CornerDownLeft className="size-5" aria-hidden="true" />
        </button>
      </div>
    </Overlay>
  )
}
