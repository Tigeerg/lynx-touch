'use client'

import { Play, Ruler, X } from 'lucide-react'
import { useId, type Dispatch } from 'react'
import type { Action } from '@/lib/cnc/machine'
import { measureFrame } from '@/lib/cnc/tool-measure'
import type { MachineState } from '@/lib/cnc/types'
import { Overlay } from './overlay'
import { MeasureReadout } from './tool-measure-dialog/measure-readout'
import { MeasureScene } from './tool-measure-dialog/measure-scene'
import { ParamFields } from './tool-measure-dialog/param-fields'

export function ToolMeasureDialog({ state, dispatch }: { state: MachineState; dispatch: Dispatch<Action> }) {
  const titleId = useId()
  const m = state.toolMeasure
  const tool = state.tools[state.cursor.tool]
  const frame = measureFrame(m)
  const close = () => dispatch({ type: 'TOOL_MEASURE_CLOSE' })
  const zNow = state.machine.Z - frame.depth * 80

  return (
    <Overlay onClose={close} labelledBy={titleId} className="w-full max-w-4xl border-2 border-foreground/20">
      <header className="flex items-center gap-2 border-b bg-muted px-4 py-2">
        <Ruler className="size-4 text-primary" aria-hidden="true" />
        <h2 id={titleId} className="text-xs font-bold uppercase tracking-wide">
          Замер длины инструмента · датчик TOOL SETTER
        </h2>
        <button
          type="button"
          tabIndex={-1}
          aria-label="Закрыть"
          className="ml-auto rounded p-1 hover:bg-accent"
          onClick={close}
        >
          <X className="size-4" aria-hidden="true" />
        </button>
      </header>

      <div className="grid grid-cols-[13rem_minmax(0,1fr)_14rem] gap-px bg-border">
        <ParamFields tool={tool} />

        <section
          aria-label="Схема замера"
          className="relative flex min-h-[22rem] items-center justify-center bg-background"
        >
          <MeasureScene frame={frame} length={m.length} />
        </section>

        <MeasureReadout frame={frame} tool={tool} zNow={zNow} elapsed={m.elapsed} length={m.length} />
      </div>

      <footer className="flex items-center gap-2 border-t bg-muted px-4 py-2">
        <p className="mr-auto text-xs text-muted-foreground">
          {m.running
            ? 'Идёт замер — шпиндель движется к датчику'
            : m.length === null
              ? 'Убедитесь, что над датчиком свободно. Enter или ПУСК — начать замер'
              : `Длина записана в таблицу: T${String(tool.number).padStart(2, '0')}`}
        </p>
        <button
          type="button"
          disabled={m.running}
          onClick={() => dispatch({ type: 'TOOL_MEASURE_START' })}
          className="flex h-10 items-center gap-2 border-2 border-primary bg-primary px-4 text-sm font-bold uppercase text-primary-foreground hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-40"
        >
          <Play className="size-4" aria-hidden="true" />
          {m.length === null ? 'Пуск замера' : 'Повторить'}
        </button>
        <button
          type="button"
          onClick={close}
          className="flex h-10 items-center gap-2 border-2 bg-card px-4 text-sm font-bold uppercase hover:bg-accent"
        >
          {m.running ? 'Прервать' : 'Закрыть'}
        </button>
      </footer>
    </Overlay>
  )
}
