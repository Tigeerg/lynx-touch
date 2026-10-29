import { useEffect, useRef } from 'react'
import { Radio } from 'lucide-react'
import { PROBE_CYCLES, PROBE_GROUPS, PROBE_TOOL } from '@/lib/cnc/probe'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { ProbeSchematic } from '../probe-schematic'

// Tool readiness badge over the scheme.
export function ProbeBadge({ state }: { state: MachineState }) {
  const ready = state.tool === PROBE_TOOL
  return (
    <div className="absolute right-4 top-4 flex items-center gap-1.5 rounded-sm border bg-card/95 px-2 py-0.5 text-[10px] font-semibold uppercase tracking-wider shadow-sm">
      <Radio className={cn('size-3.5', ready ? 'text-success' : 'text-muted-foreground')} aria-hidden="true" />
      <span className={ready ? 'text-foreground' : 'text-muted-foreground'}>
        {ready ? 'Щуп T99 · готов' : 'Щуп не установлен'}
      </span>
    </div>
  )
}

// Cycle picker grouped by feature; once the wizard has moved on, the other cycles are dimmed.
export function CycleList({ selected, locked }: { selected: number; locked: boolean }) {
  const listRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    listRef.current?.querySelector('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest' })
  }, [selected])

  return (
    <div
      ref={listRef}
      className="flex min-h-0 flex-col gap-2 overflow-y-auto bg-card p-2"
      role="listbox"
      aria-label="Циклы измерения"
    >
      {PROBE_GROUPS.map((g) => (
        <div key={g.id} className="flex flex-col gap-1">
          <p className="px-0.5 text-[10px] font-bold uppercase tracking-[0.14em] text-muted-foreground">{g.label}</p>
          <div className={cn(g.id === 'edge' ? 'grid grid-cols-2 gap-1' : 'flex flex-col gap-1')}>
            {PROBE_CYCLES.map((c, i) => ({ c, i }))
              .filter(({ c }) => c.group === g.id)
              .map(({ c, i }) => {
                const sel = i === selected
                const compact = g.id === 'edge'
                return (
                  <div
                    key={c.id}
                    role="option"
                    aria-selected={sel}
                    className={cn(
                      'flex items-center rounded-md border bg-card p-1 transition-colors',
                      compact ? 'gap-1.5' : 'gap-2',
                      sel ? 'border-primary bg-accent ring-2 ring-primary/30' : 'hover:bg-muted/60',
                      locked && !sel && 'opacity-40',
                    )}
                  >
                    <div
                      className={cn('shrink-0 rounded-sm border bg-background p-0.5', compact ? 'h-8 w-9' : 'h-8 w-11')}
                    >
                      <ProbeSchematic cycle={c} mini />
                    </div>
                    <div className="min-w-0">
                      <p className={cn('truncate text-xs leading-tight', sel ? 'font-bold' : 'font-semibold')}>
                        {compact ? c.name.replace('Кромка ', '') : c.name}
                      </p>
                      {!compact && (
                        <p className="truncate font-mono text-[10px] text-muted-foreground">
                          {c.axes.join(' · ')} · {c.touches.length} кас.
                        </p>
                      )}
                    </div>
                  </div>
                )
              })}
          </div>
        </div>
      ))}
    </div>
  )
}
