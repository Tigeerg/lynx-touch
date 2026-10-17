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

// Рабочее место «АВТО»: сверху гигантский остаток пути (Distance to Go) и оценка
// времени на остаток программы, ниже — шторка кода; по F8 «Вид» шторка сменяется
// графикой. Оценка времени без ускоренных ходов — ориентир, отсюда знак «≈».

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
        <div className="flex items-baseline gap-4">
          <span className="font-mono text-6xl font-bold leading-none tabular-nums">
            {state.motion ? dist.toFixed(3) : '—'}
          </span>
          <span className="text-sm font-medium text-muted-foreground">мм до конца кадра</span>

          <div className="ml-auto flex items-baseline gap-4">
            <div className="text-right">
              <p className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
                Осталось программы
              </p>
              <p className="font-mono text-lg font-bold tabular-nums">
                ≈ {formatDuration((rest / (state.programFeed || 1000)) * 60000)}
                <span className="ml-1 font-sans text-xs font-medium text-muted-foreground">· {rest.toFixed(1)} мм</span>
              </p>
            </div>
            <dl className="flex items-baseline gap-3">
              {AXES.map((ax) => (
                <div key={ax} className="text-right">
                  <dt className="font-mono text-[10px] font-bold uppercase text-muted-foreground">{ax}</dt>
                  <dd
                    className={cn('font-mono text-lg font-bold tabular-nums', !state.motion && 'text-muted-foreground')}
                  >
                    {state.motion ? formatCoord(dtg[ax]) : '—'}
                  </dd>
                </div>
              ))}
            </dl>
          </div>
        </div>
      </Panel>

      <div className="min-h-0 flex-1">
        {state.test.autoCode ? (
          <ProgramList state={state} dispatch={dispatch} />
        ) : (
          <ToolpathView state={state} view={pathView} className="h-full" />
        )}
      </div>
    </div>
  )
}
