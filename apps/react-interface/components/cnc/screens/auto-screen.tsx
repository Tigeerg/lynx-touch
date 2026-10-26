'use client'

import { FileCode, Route } from 'lucide-react'
import { useMemo } from 'react'
import { currentFile, distanceToGo, programLines, toWork, type Action } from '@/lib/cnc/machine'
import { MS_PER_MINUTE } from '@/lib/cnc/data'
import { remainingLength, type PathView } from '@/lib/cnc/toolpath'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatCoord, formatDuration, Lamp, Panel } from '../primitives'
import { ToolpathView, ViewBadges } from '../toolpath-view'
import { ProgramList } from './program-screen'

// The AUTO workplace: a compact Distance-to-Go strip and the time estimate; below it a single
// common "Toolpath" header and the viewer. The program code lies over the viewer as a
// transparent window on the left half; the "View" key (F8) shows and hides it.

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
  const file = currentFile(state)
  const restMs = (rest / (state.programFeed || 1000)) * MS_PER_MINUTE
  const work = toWork(state)
  const offsetCode = state.offsets[state.activeOffset].code

  return (
    <div className="flex h-full min-h-0 flex-col gap-[var(--panel-gap)]">
      <Panel title="Остаток пути" icon={Route} className="shrink-0">
        <div className="flex flex-col gap-1 px-2 py-1">
          <div className="flex items-center gap-3">
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
                ≈ {formatDuration(restMs)}
                <span className="ml-1 font-sans text-xs font-medium text-muted-foreground">· {rest.toFixed(1)} мм</span>
              </p>
            </div>
          </div>

          {/* The work DRO is permanent: the operator reads the position in AUTO even at rest, not
              only while the axes move. The offset code and the unit are part of the reading. */}
          <div className="flex items-center gap-2 border-t pt-1">
            <span className="font-mono text-xs font-bold tabular-nums text-foreground">{offsetCode}</span>
            <span className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
              Рабочие координаты · мм
            </span>
            <div className="ml-auto flex items-baseline gap-3">
              {AXES.map((ax) => (
                <span key={ax} className="flex items-baseline gap-1">
                  <span className="font-mono text-[10px] font-bold uppercase text-muted-foreground">{ax}</span>
                  <span className="font-mono text-base font-bold tabular-nums">{formatCoord(work[ax])}</span>
                  {ax === 'A' && <span className="font-sans text-[10px] text-muted-foreground">°</span>}
                </span>
              ))}
            </div>
          </div>
        </div>
      </Panel>

      <Panel
        title={`${state.test.autoView === 'gcode' ? 'Программа' : 'Траектория'} · ${file.name}`}
        icon={state.test.autoView === 'gcode' ? FileCode : Route}
        className="min-h-0 flex-1"
        right={
          <div className="flex items-center gap-3 text-xs font-medium">
            <span className="flex items-center gap-2 text-[11px]">
              <span className="flex items-center gap-1">
                <Lamp on={state.singleBlock} tone="warning" /> Покадр.
              </span>
              <span className="flex items-center gap-1">
                <Lamp on={state.optStop} tone="primary" /> M01
              </span>
              <span className="flex items-center gap-1">
                <Lamp on={state.dryRun} tone="warning" /> Холост.
              </span>
            </span>
            <span className="font-mono text-[11px] font-semibold text-muted-foreground">
              ≈ {formatDuration(restMs)}
            </span>
            {state.test.autoView !== 'gcode' && <ViewBadges view={pathView} />}
          </div>
        }
      >
        {state.test.autoView === 'gcode' ? (
          <ProgramList bare state={state} dispatch={dispatch} className="h-full" />
        ) : (
          <div className="relative h-full">
            <ToolpathView bare state={state} view={pathView} className="h-full" />

            {state.test.autoView === 'both' && (
              <div className="absolute inset-y-0 left-0 z-10 w-1/2 min-w-[320px] border-r border-border">
                <ProgramList bare state={state} dispatch={dispatch} className="h-full" />
              </div>
            )}
          </div>
        )}
      </Panel>
    </div>
  )
}
