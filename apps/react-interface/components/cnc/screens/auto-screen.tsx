'use client'

import { FileCode, Move, Route } from 'lucide-react'
import { useMemo } from 'react'
import { currentFile, distanceToGo, programLines, type Action } from '@/lib/cnc/machine'
import { AUTO_VIEW_TITLE, RAPID_RATE, SCREEN_LABEL } from '@/lib/cnc/data'
import { remainingLength, remainingTimeMs, type PathView } from '@/lib/cnc/toolpath'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatDuration, Lamp, Panel, PositionStrip } from '../primitives'
import { ToolpathView, ViewBadges } from '../toolpath-view'
import { useUiMode } from '../ui-mode-context'
import { ProgramList } from './program-screen'

// The AUTO workplace: the work position first and large, the distance to the frame and the
// remaining program below it in a quiet line; under that a single common "Toolpath" header and
// the viewer. The "View" key (F8) switches four views: the trajectory alone, the code alone,
// both side by side (stacked on the portrait touch panel, where two columns would leave each of
// them too narrow to read), and the code in a translucent window over the trajectory.

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
  const restMs = remainingTimeMs(
    programLines(state),
    state.line,
    state.programFeed || 1000,
    (RAPID_RATE * state.rapidOverride) / 100,
  )
  const touch = useUiMode()?.mode === 'touch'
  const view = state.test.autoView
  const gcode = view === 'gcode'
  const split = view === 'both'
  const overlay = view === 'overlay'

  return (
    <div className="flex h-full min-h-0 flex-col gap-[var(--panel-gap)]">
      <Panel title={SCREEN_LABEL.pos} icon={Move} className="shrink-0">
        <div className="flex flex-col gap-1 px-2 py-1">
          {/* The work DRO is the primary reading in AUTO: the operator looks at the position more
              often than at the estimate, so it takes the first line in a large face. */}
          <PositionStrip state={state} large />

          {/* The quiet line: the common distance to the frame (no per-axis split) and the
              remaining program, both secondary to the position above. */}
          <div className="flex items-baseline gap-2 border-t pt-0.5">
            <span
              className={cn('font-mono text-base font-bold tabular-nums', !state.motion && 'text-muted-foreground')}
            >
              {state.motion ? dist.toFixed(3) : '—'}
            </span>
            <span className="text-[11px] font-medium text-muted-foreground">мм до кадра</span>

            <p className="ml-auto flex items-baseline gap-1.5">
              <span className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
                Остаток пути
              </span>
              <span className="font-mono text-xs font-bold tabular-nums">
                ≈ {formatDuration(restMs)}
                <span className="ml-1 font-sans text-[11px] font-medium text-muted-foreground">
                  · {rest.toFixed(1)} мм
                </span>
              </span>
            </p>
          </div>
        </div>
      </Panel>

      <Panel
        title={`${AUTO_VIEW_TITLE[view]} · ${file.name}`}
        icon={gcode ? FileCode : Route}
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
            {!gcode && <ViewBadges view={pathView} />}
          </div>
        }
      >
        {gcode ? (
          <ProgramList bare state={state} dispatch={dispatch} className="h-full" />
        ) : split ? (
          touch ? (
            <div className="grid h-full min-h-0 grid-rows-[minmax(0,0.9fr)_minmax(0,1.1fr)] gap-1">
              {/* The code keeps ~10 readable rows: the row floor is set in fr units of the panel
                  height, and the trajectory takes the larger share below it. */}
              <ProgramList bare state={state} dispatch={dispatch} className="min-h-0 border-b" />
              <div className="relative min-h-0">
                <ToolpathView bare state={state} view={pathView} className="h-full" />
              </div>
            </div>
          ) : (
            /* The 1024 panel splits the block into two columns: the code on the left keeps a
               readable row width, the trajectory takes the larger share on the right. */
            <div className="grid h-full min-h-0 grid-cols-[minmax(0,1fr)_minmax(0,1.15fr)] gap-1">
              <ProgramList bare state={state} dispatch={dispatch} className="min-h-0 border-r" />
              <div className="relative min-h-0">
                <ToolpathView bare state={state} view={pathView} className="h-full" />
              </div>
            </div>
          )
        ) : (
          <div className="relative h-full">
            <ToolpathView bare state={state} view={pathView} className="h-full" />

            {/* The overlay view: the same code list as a translucent window on the left half,
                the trajectory visible behind and around it. */}
            {overlay && (
              <div className="absolute inset-y-0 left-0 z-10 w-1/2 min-w-[320px] border-r border-border bg-card/90 backdrop-blur-[2px]">
                <ProgramList bare state={state} dispatch={dispatch} className="h-full" />
              </div>
            )}
          </div>
        )}
      </Panel>
    </div>
  )
}
