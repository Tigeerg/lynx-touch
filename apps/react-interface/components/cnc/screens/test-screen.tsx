'use client'

import { Move } from 'lucide-react'
import { useMemo } from 'react'
import { SOFT_LIMITS } from '@/lib/cnc/data'
import { programLines, type Action } from '@/lib/cnc/machine'
import { buildToolpath, type PathView } from '@/lib/cnc/toolpath'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatCoord } from '../primitives'
import { ToolpathView } from '../toolpath-view'
import { ProgramList, SearchBar } from './program-screen'

const BBOX_AXES = ['X', 'Y', 'Z'] as const

// Габариты УП: экстремумы X/Y/Z в машинных числах (рабочие координаты программы
// плюс действующее смещение) и отметка выхода за программный предел станка.
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
  const showCurtain = !state.test.curtain
  const showViewer = !state.test.hidePath

  return (
    <div className="flex h-full min-h-0 flex-col gap-1.5">
      {state.search.open && <SearchBar state={state} dispatch={dispatch} />}

      <div className={cn('grid min-h-0 flex-1 gap-1.5', showCurtain && showViewer ? 'grid-cols-2' : 'grid-cols-1')}>
        {showCurtain && <ProgramList state={state} dispatch={dispatch} />}

        {showViewer && (
          <div className="relative min-h-0">
            <ToolpathView state={state} view={pathView} className="h-full" />
            {state.test.bbox && <BBoxOverlay state={state} />}
          </div>
        )}

        {!showCurtain && !showViewer && (
          <div className="flex h-full items-center justify-center rounded-md border bg-card text-sm text-muted-foreground">
            Графика очищена — клавиша F6 вернёт отрисовку
          </div>
        )}
      </div>
    </div>
  )
}
