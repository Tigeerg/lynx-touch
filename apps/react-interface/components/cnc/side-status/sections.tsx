import type { LucideIcon } from 'lucide-react'
import type { ReactNode } from 'react'
import { cn } from '@/lib/utils'

export function ProdStat({ icon: Icon, label, value }: { icon: LucideIcon; label: string; value: string }) {
  return (
    <div className="min-w-0">
      <dt className="flex items-center gap-1 text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
        <Icon className="size-3" aria-hidden="true" />
        {label}
      </dt>
      <dd className="font-mono text-sm font-bold tabular-nums">{value}</dd>
    </div>
  )
}

export function Section({
  icon: Icon,
  title,
  right,
  children,
  area,
  className,
}: {
  icon: LucideIcon
  title: string
  right?: ReactNode
  children: ReactNode
  /** Named zone in the fixed side grid (WIDE_SECTIONS): tool, load, codes, correctors, execution. */
  area?: string
  className?: string
}) {
  return (
    <section
      style={area ? { gridArea: area } : undefined}
      // The blocks keep their content height (`shrink-0`): a short column scrolls instead of
      // crushing the last block, so the operator never reads half a number.
      className={cn('flex shrink-0 flex-col gap-0.5 border-b px-2.5 last:border-b-0', className)}
    >
      <header className="flex items-center gap-1.5 text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
        <Icon className="size-3.5" aria-hidden="true" />
        <h3>{title}</h3>
        {right && (
          <div className="ml-auto flex items-center gap-1.5 text-xs font-medium normal-case tracking-normal text-foreground">
            {right}
          </div>
        )}
      </header>
      {children}
    </section>
  )
}

export function Reading({
  value,
  unit,
  set,
  setLabel,
}: {
  value: number
  unit: string
  set: number
  setLabel: string
}) {
  return (
    <div className="flex items-baseline justify-between gap-2">
      <p className="font-mono text-lg font-bold tabular-nums leading-none">
        {value}
        <span className="ml-1 font-sans text-xs font-medium text-muted-foreground">{unit}</span>
      </p>
      <p className="font-mono text-sm tabular-nums text-muted-foreground">
        {setLabel} <span className="font-bold text-foreground">{set}</span>
      </p>
    </div>
  )
}

// The step key of the touch deck: the panel variant shows the physical `[`/`]` caps, the touch one
// has no keys to name, so it draws a finger-sized button in the same place.
export const STEP_KEY =
  'flex h-8 w-9 shrink-0 select-none items-center justify-center rounded-md border border-b-[3px] bg-card text-base text-muted-foreground active:translate-y-px active:border-b'

// The classic 1024 panel: one column in the operator's reading order (tool, correctors, active codes,
// load, execution). The blocks are separated by the same 1 px line as before, so the five of them fit
// inside the frame without a scrollbar; the room the operator asked for goes into each block's own
// inner padding, not into a gap between the frames.
// The column itself is painted like a card, so the spare height below the last block reads as part
// of it instead of a grey strip; blocks are divided by a hairline, not by a background gap.
const PANEL_SECTION = '[&>section]:bg-card [&>section]:px-2 [&>section]:py-0.5 [&>section:not(:last-child)]:border-b'

export const PANEL_SECTIONS = `flex h-full min-h-0 flex-col overflow-hidden bg-card ${PANEL_SECTION}`

// The touch column keeps the two-column grid it had: the deck below carries the correctors buttons,
// and the blocks must not reflow into a scrolling column on the 1080x1920 panel.
export const TOUCH_SECTIONS =
  "grid h-full grid-cols-2 grid-rows-[9rem_minmax(0,1fr)_auto] gap-px overflow-hidden bg-border [grid-template-areas:'tool_load'_'codes_correctors'_'execution_execution'] [&>section]:border-0 [&>section]:bg-card [&>section]:py-1"

export const TOUCH_SECTIONS_NO_EXEC =
  "grid h-full grid-cols-2 grid-rows-[9rem_minmax(0,1fr)] gap-px overflow-hidden bg-border [grid-template-areas:'tool_load'_'codes_correctors'] [&>section]:border-0 [&>section]:bg-card [&>section]:py-1"

// The status column still flows in reading order: it has no fixed zones.
export const SIDE_FLOW =
  'grid h-full grid-cols-2 content-start overflow-y-auto [&>section:nth-child(odd)]:border-r [&>section]:py-1.5'
