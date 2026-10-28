import type { LucideIcon } from 'lucide-react'
import type { ReactNode } from 'react'
import { toWork } from '@/lib/cnc/machine'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

export function Keycap({ children, className }: { children: ReactNode; className?: string }) {
  return (
    <kbd
      className={cn(
        'inline-flex h-6 min-w-6 shrink-0 items-center justify-center rounded-[4px] border border-b-[3px] border-foreground/20 bg-card px-1.5 font-mono text-[11px] font-semibold leading-none text-foreground/80',
        className,
      )}
    >
      {children}
    </kbd>
  )
}

export function Panel({
  title,
  icon: Icon,
  right,
  children,
  className,
}: {
  title: string
  icon?: LucideIcon
  right?: ReactNode
  children: ReactNode
  className?: string
}) {
  return (
    <section className={cn('cnc-box flex min-h-0 flex-col overflow-hidden rounded-md border bg-card', className)}>
      <header className="flex h-8 shrink-0 items-center gap-2 border-b bg-muted px-2.5 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
        {Icon && <Icon className="size-4 shrink-0" aria-hidden="true" />}
        <h2 className="min-w-0 truncate">{title}</h2>
        {right && <div className="ml-auto flex shrink-0 items-center gap-2 normal-case tracking-normal">{right}</div>}
      </header>
      <div className="min-h-0 flex-1">{children}</div>
    </section>
  )
}

export function Lamp({
  on,
  tone = 'success',
  className,
}: {
  on: boolean
  tone?: 'success' | 'warning' | 'destructive' | 'primary'
  className?: string
}) {
  const toneClass = {
    success: 'bg-success',
    warning: 'bg-warning',
    destructive: 'bg-destructive',
    primary: 'bg-primary',
  }[tone]
  return (
    <span
      aria-hidden="true"
      className={cn(
        'inline-block size-2.5 shrink-0 rounded-full border border-foreground/15',
        on ? toneClass : 'bg-muted',
        className,
      )}
    />
  )
}

export function OverrideBar({
  value,
  max,
  mark = 100,
  className,
}: {
  value: number
  max: number
  mark?: number
  /** Touch deck draws the same bar a little taller; the panel keeps the stock height. */
  className?: string
}) {
  const pct = Math.min(100, (value / max) * 100)
  const tone = value > mark ? 'bg-warning' : value < mark ? 'bg-primary/60' : 'bg-primary'
  return (
    <div
      className={cn('relative h-3 w-full overflow-hidden rounded-sm border bg-muted', className)}
      role="presentation"
    >
      <div className={cn('h-full transition-[width] duration-150', tone)} style={{ width: `${pct}%` }} />
      <div className="absolute inset-y-0 w-px bg-foreground/50" style={{ left: `${(mark / max) * 100}%` }} />
    </div>
  )
}

export function formatCoord(v: number, digits = 3) {
  const rounded = Math.abs(v) < 0.5 * 10 ** -digits ? 0 : v
  return `${rounded < 0 ? '−' : '+'}${Math.abs(rounded).toFixed(digits)}`
}

// DRO reading: the sign is shown only when the number is negative (`−120.500`), a positive
// position needs no `+` in front of it. Rounding matches formatCoord.
export function formatDro(v: number, digits = 3) {
  const rounded = Math.abs(v) < 0.5 * 10 ** -digits ? 0 : v
  return `${rounded < 0 ? '−' : ''}${Math.abs(rounded).toFixed(digits)}`
}

// The position strip: the work position in the active work offset, readable at rest and on every
// screen that shows it (AUTO and PROGON). One markup for both places, so the offset code and the
// unit never drift apart. `large` is the AUTO reading: the coordinates are the primary number
// there, the rest of the panel is secondary.
export function PositionStrip({
  state,
  compact = false,
  large = false,
  className,
}: {
  state: MachineState
  compact?: boolean
  large?: boolean
  className?: string
}) {
  const work = toWork(state)
  const offsetCode = state.offsets[state.activeOffset].code
  return (
    <div
      role="group"
      aria-label={`Рабочие координаты в ${offsetCode}, мм`}
      className={cn('flex min-w-0 items-center', large ? 'gap-3' : 'items-baseline gap-2', className)}
    >
      <span className={cn('shrink-0 font-mono font-bold tabular-nums text-foreground', large ? 'text-sm' : 'text-xs')}>
        {offsetCode}
      </span>
      <span
        className={cn(
          'shrink-0 font-semibold uppercase tracking-wider text-muted-foreground',
          large ? 'text-xs' : 'text-[10px]',
        )}
      >
        Рабочие · мм
      </span>
      <div className={cn('ml-auto flex min-w-0 items-baseline', large ? 'gap-4' : 'gap-2.5')}>
        {AXES.map((ax) => (
          <span key={ax} className="flex items-baseline gap-1">
            <span className="font-mono text-[10px] font-bold uppercase text-muted-foreground">{ax}</span>
            <span
              className={cn(
                'font-mono font-bold tabular-nums',
                large ? 'text-3xl leading-none' : compact ? 'text-sm' : 'text-base',
              )}
            >
              {formatDro(work[ax])}
            </span>
            {ax === 'A' && (
              <span className={cn('font-sans text-muted-foreground', large ? 'text-xs' : 'text-[10px]')}>°</span>
            )}
          </span>
        ))}
      </div>
    </div>
  )
}

export function formatTime(ms: number) {
  const total = Math.floor(ms / 1000)
  const h = Math.floor(total / 3600)
  const m = Math.floor((total % 3600) / 60)
  const sec = total % 60
  return [h, m, sec].map((n) => String(n).padStart(2, '0')).join(':')
}

/** A short duration in minutes and seconds: for the machining-time estimate. */
export function formatDuration(ms: number) {
  const total = Math.max(0, Math.round(ms / 1000))
  return [Math.floor(total / 60), total % 60].map((n) => String(n).padStart(2, '0')).join(':')
}
