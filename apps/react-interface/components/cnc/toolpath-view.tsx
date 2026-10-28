'use client'

import { Route } from 'lucide-react'
import { useMemo } from 'react'
import { programLines, toWork } from '@/lib/cnc/machine'
import { buildToolpath, PATH_VIEWS, project, simPose, type PathView } from '@/lib/cnc/toolpath'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap, Panel } from './primitives'

const VIEW_LABEL: Record<PathView, string> = { ISO: 'Изометрия', XY: 'План XY', XZ: 'Вид XZ' }

// View-angle switch with the `V` key hint. Shared by the viewer panel and the common header
// in AUTO.
export function ViewBadges({ view }: { view: PathView }) {
  return (
    <div className="flex items-center gap-1.5 text-xs font-medium">
      <Keycap>V</Keycap>
      <div className="flex overflow-hidden rounded-sm border">
        {PATH_VIEWS.map((v) => (
          <span
            key={v}
            className={cn(
              'px-2 py-0.5 font-mono text-[11px] font-bold',
              v === view ? 'bg-primary text-primary-foreground' : 'bg-card text-muted-foreground',
            )}
          >
            {v}
          </span>
        ))}
      </div>
    </div>
  )
}

export function ToolpathView({
  state,
  view,
  className,
  bare = false,
}: {
  state: MachineState
  view: PathView
  className?: string
  /** Without its own frame and header: the viewer content for the common header. */
  bare?: boolean
}) {
  const lines = programLines(state)
  const segments = useMemo(() => buildToolpath(lines), [lines])
  const sim = state.sim.active
  // A stopped machine has no moving tool to follow: the marker and the highlighted block follow
  // the operator's line selection (state.cursor.prog) along the same geometry as the simulation,
  // while the machine itself does not move. RUN/HOLD and the simulation keep their own line.
  const select = !sim && state.run === 'IDLE'
  const pose = useMemo(
    () => (sim ? simPose(segments, state.sim.line) : select ? simPose(segments, state.cursor.prog) : null),
    [sim, select, segments, state.sim.line, state.cursor.prog],
  )
  const toolPt = pose ? pose.pos : toWork(state)
  const cursorLine = sim ? state.sim.line : select ? state.cursor.prog : state.line

  const geometry = useMemo(() => {
    const pts = segments.flatMap((s) => [project(s.from, view), project(s.to, view)])
    if (pts.length === 0) return null
    const xs = pts.map((p) => p[0])
    const ys = pts.map((p) => p[1])
    const minX = Math.min(...xs)
    const minY = Math.min(...ys)
    const w = Math.max(1, Math.max(...xs) - minX)
    const h = Math.max(1, Math.max(...ys) - minY)
    const pad = Math.max(w, h) * 0.08
    return { minX: minX - pad, minY: minY - pad, w: w + pad * 2, h: h + pad * 2, unit: Math.max(w, h) / 100 }
  }, [segments, view])

  const rawTool = project({ X: toolPt.X, Y: toolPt.Y, Z: toolPt.Z }, view)
  const inset = geometry ? geometry.unit * 2.5 : 0
  const tool: [number, number] = geometry
    ? [
        Math.min(geometry.minX + geometry.w - inset, Math.max(geometry.minX + inset, rawTool[0])),
        Math.min(geometry.minY + geometry.h - inset, Math.max(geometry.minY + inset, rawTool[1])),
      ]
    : rawTool
  const origin = project({ X: 0, Y: 0, Z: 0 }, view)
  const feedCount = segments.filter((s) => !s.rapid).length

  const content = (
    <div className="toolpath-grid relative h-full bg-[radial-gradient(var(--border)_1px,transparent_1px)] [background-size:16px_16px]">
      {geometry ? (
        <svg
          viewBox={`${geometry.minX} ${geometry.minY} ${geometry.w} ${geometry.h}`}
          preserveAspectRatio="xMidYMid meet"
          className="absolute inset-0 size-full p-2"
          role="img"
          aria-label={`${VIEW_LABEL[view]}: ${segments.length} перемещений, из них ${feedCount} рабочих`}
        >
          <g className="stroke-muted-foreground/60" strokeWidth={1} vectorEffect="non-scaling-stroke">
            <line
              x1={origin[0]}
              y1={origin[1]}
              x2={origin[0] + geometry.unit * 8}
              y2={origin[1]}
              vectorEffect="non-scaling-stroke"
              className="stroke-destructive"
              strokeWidth={2}
            />
            <line
              x1={origin[0]}
              y1={origin[1]}
              x2={origin[0]}
              y2={origin[1] - geometry.unit * 8}
              vectorEffect="non-scaling-stroke"
              className="stroke-success"
              strokeWidth={2}
            />
          </g>
          {segments.map((s, i) => {
            const [x1, y1] = project(s.from, view)
            const [x2, y2] = project(s.to, view)
            const done = s.line < cursorLine
            const current = s.line === cursorLine
            return (
              <line
                key={i}
                x1={x1}
                y1={y1}
                x2={x2}
                y2={y2}
                vectorEffect="non-scaling-stroke"
                strokeLinecap="round"
                strokeWidth={current ? 3.5 : s.rapid ? 1 : 2}
                strokeDasharray={s.rapid ? '4 4' : undefined}
                className={cn(
                  current
                    ? 'stroke-warning'
                    : s.rapid
                      ? 'stroke-muted-foreground/70'
                      : done
                        ? 'stroke-primary'
                        : 'stroke-primary/30',
                )}
              />
            )
          })}
          <circle
            cx={tool[0]}
            cy={tool[1]}
            r={geometry.unit * 2.2}
            className="fill-destructive/20 stroke-destructive"
            strokeWidth={2}
            vectorEffect="non-scaling-stroke"
          />
          <circle cx={tool[0]} cy={tool[1]} r={geometry.unit * 0.6} className="fill-destructive" />
        </svg>
      ) : (
        <p className="flex h-full items-center justify-center text-sm text-muted-foreground">
          Нет перемещений в программе
        </p>
      )}

      <div className="pointer-events-none absolute bottom-2 right-2 flex items-center gap-3 rounded-sm border bg-card/90 px-2 py-1 text-[11px] font-medium">
        <span className="font-semibold">{VIEW_LABEL[view]}</span>
        <Legend className="border-primary" label="Рабочий" />
        <Legend className="border-dashed border-muted-foreground" label="Ускоренный" />
        <Legend className="border-warning" label="Кадр" />
        <span className="flex items-center gap-1">
          <span className="size-2.5 rounded-full border-2 border-destructive" aria-hidden="true" />
          Инструмент
        </span>
      </div>
    </div>
  )

  if (bare) return <div className={cn('relative h-full overflow-hidden', className)}>{content}</div>

  return (
    <Panel title="Траектория" icon={Route} className={className} right={<ViewBadges view={view} />}>
      {content}
    </Panel>
  )
}

function Legend({ className, label }: { className: string; label: string }) {
  return (
    <span className="flex items-center gap-1">
      <span className={cn('w-4 border-t-2', className)} aria-hidden="true" />
      {label}
    </span>
  )
}
