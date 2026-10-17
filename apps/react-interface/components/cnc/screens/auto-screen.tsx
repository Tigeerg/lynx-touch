'use client'

import { Route } from 'lucide-react'
import { useMemo } from 'react'
import { distanceToGo, programLines, type Action } from '@/lib/cnc/machine'
import { remainingLength, type PathView } from '@/lib/cnc/toolpath'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatCoord, formatDuration, Panel } from '../primitives'
import { ToolpathView } from '../toolpath-view'
import { ProgramList } from './program-screen'

// Рабочее место «АВТО»: компактная полоса остатка пути (Distance to Go) и оценка
// времени; под ней всегда вьювер траектории, а код программы ложится поверх него
// тёмной полупрозрачной подложкой. Клавиша «Вид» (F8) показывает и убирает код.

export function AutoScreen({
  state,
  dispatch,
  pathView,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  pathView: PathView
}) {
  const dtg = distanceToGo(state)
  const dist = Math.hypot(dtg.X, dtg.Y, dtg.Z)
  const rest = useMemo(
    () => remainingLength(programLines(state), state.line),
    [state.files, state.activeFile, state.line],
  )

  return (
    <div className="flex h-full min-h-0 flex-col gap-1.5">
      <Panel title="Остаток пути" icon={Route} className="shrink-0">
        <div className="flex items-center gap-3 px-2 py-1">
          <span className="font-mono text-3xl font-bold leading-none tabular-nums">
            {state.motion ? dist.toFixed(3) : '—'}
          </span>
          <span className="text-xs font-medium text-muted-foreground">мм до кадра</span>

          <div className="ml-2 flex items-baseline gap-3">
            {AXES.map((ax) => (
              <span key={ax} className="flex items-baseline gap-1">
                <span className="font-mono text-[10px] font-bold uppercase text-muted-foreground">{ax}</span>
                <span
                  className={cn('font-mono text-sm font-bold tabular-nums', !state.motion && 'text-muted-foreground')}
                >
                  {state.motion ? formatCoord(dtg[ax]) : '—'}
                </span>
              </span>
            ))}
          </div>

          <div className="ml-auto text-right leading-tight">
            <p className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
              Осталось программы
            </p>
            <p className="font-mono text-sm font-bold tabular-nums">
              ≈ {formatDuration((rest / (state.programFeed || 1000)) * 60000)}
              <span className="ml-1 font-sans text-xs font-medium text-muted-foreground">· {rest.toFixed(1)} мм</span>
            </p>
          </div>
        </div>
      </Panel>

      <div className="relative min-h-0 flex-1">
        <ToolpathView state={state} view={pathView} className="h-full" />

        {state.test.autoCode && (
          <div className="absolute inset-y-0 left-0 z-10 w-1/2 min-w-[320px]">
            <ProgramList
              state={state}
              dispatch={dispatch}
              className="border-transparent bg-transparent [&>header]:bg-transparent"
            />
          </div>
        )}
      </div>
    </div>
  )
}
