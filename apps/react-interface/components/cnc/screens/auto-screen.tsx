'use client'

import { Route } from 'lucide-react'
import { distanceToGo, type Action } from '@/lib/cnc/machine'
import type { PathView } from '@/lib/cnc/toolpath'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { formatCoord, Panel } from '../primitives'
import { ToolpathView } from '../toolpath-view'
import { ProgramList } from './program-screen'

// Рабочее место «АВТО»: сверху гигантский остаток пути (Distance to Go), ниже —
// шторка кода на весь остаток площади; по F8 «Вид» шторка сменяется графикой.
export function AutoScreen({
  state,
  dispatch,
  pathView,
  showPath,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  pathView: PathView
  showPath: boolean
}) {
  const dtg = distanceToGo(state)
  const dist = Math.hypot(dtg.X, dtg.Y, dtg.Z)

  return (
    <div className="flex h-full min-h-0 flex-col gap-1.5">
      <Panel title="Остаток пути" icon={Route} className="shrink-0">
        <div className="flex items-baseline gap-4">
          <span className="font-mono text-6xl font-bold leading-none tabular-nums">{formatCoord(dist)}</span>
          <span className="text-sm font-medium text-muted-foreground">мм до конца кадра</span>
          <dl className="ml-auto flex items-baseline gap-4">
            {AXES.map((ax) => (
              <div key={ax} className="text-right">
                <dt className="font-mono text-[10px] font-bold uppercase text-muted-foreground">{ax}</dt>
                <dd className="font-mono text-lg font-bold tabular-nums">{formatCoord(dtg[ax])}</dd>
              </div>
            ))}
          </dl>
        </div>
      </Panel>

      <div className="min-h-0 flex-1">
        {showPath ? (
          <ToolpathView state={state} view={pathView} className="h-full" />
        ) : (
          <ProgramList state={state} dispatch={dispatch} />
        )}
      </div>
    </div>
  )
}
