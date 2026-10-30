'use client'

// The 2D XY preview of the quick program (PLAN-QUICKPROG, 7.4, C5).
//
// The lines are the emitted file, the same array `CAM_SAVE` writes, and the segments come from
// `buildToolpath` — one source of geometry, the preview cannot disagree with the saved program.
// `ToolpathView` was not reused: it reads `MachineState` (the active file, the cursor, the tool
// pose), and showing the buffer during a run would draw the machine, not the program being built.
import { useMemo } from 'react'
import { buildToolpath } from '@/lib/cnc/toolpath'
import { formatMm } from './cam-labels'

const W = 100
const H = 100
const PAD = 8

export function CamPreview({ preview }: { preview: string[] | null }) {
  const geometry = useMemo(() => {
    const segments = buildToolpath(preview ?? [])
    if (segments.length === 0) return null
    const xs = segments.flatMap((s) => [s.from.X, s.to.X])
    const ys = segments.flatMap((s) => [s.from.Y, s.to.Y])
    const [xMin, xMax] = [Math.min(...xs), Math.max(...xs)]
    const [yMin, yMax] = [Math.min(...ys), Math.max(...ys)]
    const span = Math.max(1, xMax - xMin, yMax - yMin)
    const scale = (W - PAD * 2) / span
    const cx = (xMin + xMax) / 2
    const cy = (yMin + yMax) / 2
    // SVG Y grows down, the machine Y grows up: the sign flips around the middle of the box.
    const pt = (x: number, y: number) => [W / 2 + (x - cx) * scale, H / 2 - (y - cy) * scale] as const
    const rapid = segments
      .filter((s) => s.rapid)
      .map((s) => `${pt(s.from.X, s.from.Y).join(',')} ${pt(s.to.X, s.to.Y).join(',')}`)
      .join(' ')
    const cut = segments
      .filter((s) => !s.rapid)
      .map((s) => `${pt(s.from.X, s.from.Y).join(',')} ${pt(s.to.X, s.to.Y).join(',')}`)
      .join(' ')
    return {
      rapid,
      cut,
      size: `${formatMm(xMax - xMin)}×${formatMm(yMax - yMin)}`,
      cuts: segments.length - rapid.length,
    }
  }, [preview])

  return (
    <section aria-label="Предпросмотр траектории" className="flex min-h-0 flex-1 flex-col rounded-md border bg-card">
      <header className="flex shrink-0 items-baseline justify-between border-b px-2 py-1">
        <h3 className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">Вид XY</h3>
        <span className="font-mono text-[10px] tabular-nums text-muted-foreground">
          {geometry ? `${geometry.size} мм` : 'пусто'}
        </span>
      </header>
      {geometry ? (
        <svg
          viewBox={`0 0 ${String(W)} ${String(H)}`}
          className="min-h-0 w-full flex-1 bg-background"
          role="img"
          aria-label={`Траектория XY, габарит ${geometry.size} мм, резание ${String(geometry.cuts)} ходов`}
        >
          {/* The axes cross at the work origin, under the path, so the operator reads the frame. */}
          <line x1="0" y1={H / 2} x2={W} y2={H / 2} className="stroke-border" strokeWidth="0.3" />
          <line x1={W / 2} y1="0" x2={W / 2} y2={H} className="stroke-border" strokeWidth="0.3" />
          <polyline
            points={geometry.rapid}
            fill="none"
            className="stroke-warning"
            strokeWidth="0.5"
            strokeDasharray="1.5 1"
          />
          <polyline points={geometry.cut} fill="none" className="stroke-primary" strokeWidth="0.8" />
        </svg>
      ) : (
        <p className="flex min-h-0 flex-1 items-center justify-center p-3 text-center text-[11px] text-muted-foreground">
          Траектории пока нет: выберите операцию, и она появится здесь
        </p>
      )}
      <footer className="flex shrink-0 items-center gap-3 border-t px-2 py-1 text-[10px] text-muted-foreground">
        <span className="flex items-center gap-1">
          <span className="h-0.5 w-4 border-t-2 border-primary" aria-hidden="true" /> резание
        </span>
        <span className="flex items-center gap-1">
          <span className="h-0.5 w-4 border-t-2 border-dashed border-warning" aria-hidden="true" /> быстрый ход
        </span>
      </footer>
    </section>
  )
}
