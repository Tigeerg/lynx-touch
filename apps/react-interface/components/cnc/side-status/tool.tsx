import { SlidersHorizontal, Wrench } from 'lucide-react'
import { SCREEN_LABEL } from '@/lib/cnc/data'
import { programTools } from '@/lib/cnc/gcode'
import { toolPreview } from '@/lib/cnc/tool-preview'
import { programLines, type Action } from '@/lib/cnc/machine'
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM, paramValue } from '@/lib/cnc/service-data'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatTime, Keycap, OverrideBar } from '../primitives'
import { STEP_KEY, Section } from './sections'
import { RapidRow } from './load'

// The one "Tool" block of the side column, the same in AUTO and SETUP: the header names the tool
// in the spindle, the body lists the tools the program itself calls (up to three). The current
// tool is highlighted and carries its geometry (D, L) on a second line; a program without tool
// changes still shows the tool in the spindle, so the block is never empty.
export function ToolSection({
  state,
  area = 'tool',
  className,
}: {
  state: MachineState
  /** Named zone in the fixed side grid (WIDE_SECTIONS): "tool". */
  area?: string
  className?: string
}) {
  // The block follows the spindle: the current tool and the next one the program calls, so the
  // operator can prepare it in advance. A program with several tools no longer shows only the first.
  const list = toolPreview(programTools(programLines(state)), state.tool)

  return (
    <Section
      icon={Wrench}
      title={SCREEN_LABEL.tool}
      area={area}
      className={className}
      right={<span className="font-mono font-bold">T{String(state.tool).padStart(2, '0')}</span>}
    >
      <ul className="flex flex-col gap-0.5">
        {list.map((n) => {
          const tool = state.tools.find((x) => x.number === n)
          const current = n === state.tool
          return (
            <li key={n} className={cn('flex flex-col gap-0.5 rounded-sm px-1 py-0.5', current && 'bg-accent')}>
              <div className="flex items-center gap-1.5">
                <span
                  className={cn(
                    'flex h-5 w-8 shrink-0 items-center justify-center rounded-sm font-mono text-[10px] font-bold',
                    current ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
                  )}
                >
                  T{String(n).padStart(2, '0')}
                </span>
                <span className="min-w-0 flex-1 truncate text-[11px] font-semibold">
                  {tool?.name ?? 'Не найден в таблице'}
                  {current && <span className="sr-only"> (в шпинделе)</span>}
                </span>
                <span className="shrink-0 font-mono text-[10px] tabular-nums text-muted-foreground" title="Наработка">
                  {formatTime(state.toolTime[n] ?? 0)}
                </span>
                {tool && (
                  <span
                    className={cn(
                      'shrink-0 font-mono text-[10px] font-bold tabular-nums',
                      tool.life < 10 ? 'text-destructive' : 'text-muted-foreground',
                    )}
                    title="Остаточный ресурс"
                  >
                    {tool.life}%
                  </span>
                )}
              </div>
              {current && (
                <p className="pl-[2.4rem] font-mono text-[10px] tabular-nums text-muted-foreground">
                  D{tool?.diameter ?? '—'} · L {tool ? tool.length.toFixed(3) : '—'}
                </p>
              )}
            </li>
          )
        })}
      </ul>
    </Section>
  )
}

export function OverrideRow({
  label,
  value,
  max,
  keys,
  onStep,
  touch = false,
}: {
  label: string
  value: number
  max: number
  /** Physical keys that drive the value; on the touch deck they are replaced by the buttons. */
  keys: [string, string]
  /** Present on the touch deck: the row steps by 10 % on a tap, like the keys do. */
  onStep?: (dir: 1 | -1) => void
  /** Touch sizes only: a bigger label and a taller bar. */
  touch?: boolean
}) {
  return (
    <div className={cn('flex items-center gap-2', touch ? 'text-sm' : 'text-xs')}>
      <span className={cn('shrink-0 font-semibold text-muted-foreground', touch ? 'w-8' : 'w-6')}>{label}</span>
      {onStep ? (
        <button
          type="button"
          tabIndex={-1}
          aria-label={`${label} меньше`}
          onClick={() => onStep(-1)}
          className={STEP_KEY}
        >
          −
        </button>
      ) : (
        <Keycap className="h-5 min-w-5 px-1 text-[10px]">{keys[0]}</Keycap>
      )}
      <div className="flex-1">
        <OverrideBar value={value} max={max} className={touch ? 'h-3.5' : undefined} />
      </div>
      {onStep ? (
        <button
          type="button"
          tabIndex={-1}
          aria-label={`${label} больше`}
          onClick={() => onStep(1)}
          className={STEP_KEY}
        >
          +
        </button>
      ) : (
        <Keycap className="h-5 min-w-5 px-1 text-[10px]">{keys[1]}</Keycap>
      )}
      <span
        className={cn(
          'shrink-0 text-right font-mono font-bold tabular-nums',
          touch ? 'w-14 text-base' : 'w-10 text-sm',
        )}
      >
        {value}%
      </span>
    </div>
  )
}

// The one "Correctors" block of the side column, shared by AUTO and SETUP: spindle, feed and rapid
// overrides in the same order. The touch variant swaps the key caps for finger-sized buttons.
export function CorrectorsSection({
  state,
  dispatch,
  touch = false,
  area = 'correctors',
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  touch?: boolean
  /** Named zone in the side grid; both screens use "correctors". */
  area?: string
  className?: string
}) {
  return (
    <Section icon={SlidersHorizontal} title="Корректоры" area={area} className={className}>
      <OverrideRow
        label="S%"
        value={state.spindleOverride}
        max={paramValue(state.params, MAX_SPINDLE_PARAM)}
        keys={['[', ']']}
        touch={touch}
        onStep={touch ? (dir) => dispatch({ type: 'OVERRIDE', kind: 'spindle', dir }) : undefined}
      />
      <OverrideRow
        label="F%"
        value={state.feedOverride}
        max={paramValue(state.params, MAX_FEED_PARAM)}
        keys={['−', '=']}
        touch={touch}
        onStep={touch ? (dir) => dispatch({ type: 'OVERRIDE', kind: 'feed', dir }) : undefined}
      />
      <RapidRow
        value={state.rapidOverride}
        touch={touch}
        onStep={touch ? (dir) => dispatch({ type: 'OVERRIDE', kind: 'rapid', dir }) : undefined}
      />
    </Section>
  )
}
