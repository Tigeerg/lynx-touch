'use client'

import { Move } from 'lucide-react'
import { useMemo } from 'react'
import { SOFT_LIMITS } from '@/lib/cnc/data'
import { programLines, type Action } from '@/lib/cnc/machine'
import { buildToolpath, type PathView } from '@/lib/cnc/toolpath'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatCoord, PositionStrip } from '../primitives'
import { ToolpathView } from '../toolpath-view'
import { useUiMode } from '../ui-mode-context'
import { ProgramList, SearchBar } from './program-screen'

const BBOX_AXES = ['X', 'Y', 'Z'] as const

// NC bounding box: the X/Y/Z extremes in machine numbers (the program work coordinates plus
// the work offset in force) and a mark when a soft limit is exceeded.
function BBoxOverlay({ state }: { state: MachineState }) {
  const lines = programLines(state)
  const segments = useMemo(() => buildToolpath(lines), [lines])
  const off = state.offsets[state.activeOffset].values
  if (segments.length === 0) return null

  const ext = BBOX_AXES.map((ax) => {
    const values = segments.flatMap((s) => [s.from[ax], s.to[ax]])
    const min = Math.min(...values) + off[ax]
    const max = Math.max(...values) + off[ax]
    const [lo, hi] = SOFT_LIMITS[ax]
    return { ax, min, max, out: min < lo || max > hi }
  })

  return (
    <div className="absolute right-2 top-2 z-10 rounded-md border bg-card/95 px-3 py-2 shadow-sm">
      <p className="mb-1 flex items-center gap-1.5 text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
        <Move className="size-3.5" aria-hidden="true" />
        Габариты УП, мм
      </p>
      <ul className="flex flex-col gap-0.5 font-mono text-xs">
        {ext.map((e) => (
          <li
            key={e.ax}
            className={cn('flex items-center gap-2 tabular-nums', e.out ? 'font-bold text-destructive' : undefined)}
          >
            <span className="w-3 font-bold">{e.ax}</span>
            <span>{formatCoord(e.min)}</span>
            <span className="text-muted-foreground">..</span>
            <span>{formatCoord(e.max)}</span>
            {e.out && <span className="font-sans text-[10px]">за пределом</span>}
          </li>
        ))}
      </ul>
    </div>
  )
}

export function TestScreen({
  state,
  dispatch,
  pathView,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  pathView: PathView
}) {
  const touch = useUiMode()?.mode === 'touch'
  const view = state.test.autoView
  const both = view === 'both'
  const overlay = view === 'overlay'
  const showCode = both || view === 'gcode'
  const showViewer = view !== 'gcode'

  return (
    <div className="flex h-full min-h-0 flex-col gap-1.5">
      {state.search.open && <SearchBar state={state} dispatch={dispatch} />}

      {/* Only the work coordinates stay above the program: the run switches of PROGON live on
          F4-F8 (mode, block modes, NC bounding box, view), not on a second copy of the strip.
          The large face keeps the coordinate block tall enough to read from a step back, in
          simulation as well as in a dry run. */}
      <div className="flex shrink-0 items-center rounded-md border bg-card px-3 py-2">
        <PositionStrip state={state} large className="min-w-0" />
      </div>

      {/* Both on the portrait touch panel: the code above, the trajectory below, each at full
          width. The code row keeps a floor of ten block rows (a touch row is 44 px plus the panel
          header), so reading the program does not shrink to a keyhole; the rest goes to the
          graphics. The 1024 panel keeps its two columns. The overlay view puts the same list as a
          translucent window over the graphics. */}
      <div
        className={cn(
          'grid min-h-0 flex-1 gap-1.5',
          both
            ? touch
              ? 'grid-cols-1 grid-rows-[minmax(30rem,0.9fr)_minmax(0,1.1fr)]'
              : 'grid-cols-2'
            : 'grid-cols-1',
        )}
      >
        {showCode && <ProgramList state={state} dispatch={dispatch} />}

        {showViewer && (
          <div className="relative min-h-0">
            <ToolpathView state={state} view={pathView} className="h-full" />
            {state.test.bbox && <BBoxOverlay state={state} />}
            {overlay && (
              <div className="up-overlay absolute bottom-0 left-0 top-8 z-10 w-1/2 min-w-[18rem]">
                <ProgramList bare state={state} dispatch={dispatch} className="h-full" />
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  )
}
