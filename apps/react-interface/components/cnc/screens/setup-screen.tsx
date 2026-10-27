import { Move, Radio, SlidersHorizontal } from 'lucide-react'
import { toWork, type Action } from '@/lib/cnc/machine'
import { PROBE_STEP_MEASURE, PROBE_TOOL } from '@/lib/cnc/probe'
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM, paramValue } from '@/lib/cnc/service-data'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { MdiInput } from '../mdi-input'
import { ActiveCodes } from '../program-side'
import { formatCoord, Lamp, Panel } from '../primitives'
import { OperatorPad, OverrideRow, RapidRow, Section, ToolSection, WIDE_SECTIONS } from '../side-status'

// The SETUP workplace: on the left the large work and machine coordinates, the MDI line and
// the probe indication; on the right the tool, the active codes, the overrides. The toolpath
// graphics are hidden here: setup goes by the numbers. The load is not shown in setup: the
// operator reads it in AUTO while cutting, not while preparing.

function Indicator({ label, on }: { label: string; on: boolean }) {
  return (
    <li className="flex items-center gap-2">
      <Lamp on={on} />
      <span className={cn('font-medium', on ? 'text-foreground' : 'text-muted-foreground')}>{label}</span>
    </li>
  )
}

function ProbeSensors({ state }: { state: MachineState }) {
  return (
    <Panel title="Датчики щупа" icon={Radio} className="w-[248px] shrink-0">
      <ul className="flex flex-col gap-1.5 p-2 text-xs">
        <Indicator label="Щуп T99 в шпинделе" on={state.tool === PROBE_TOOL} />
        <Indicator label="Измерение идёт" on={state.probe.running} />
        <Indicator label="Есть результат" on={state.probe.result !== null && state.probe.step >= PROBE_STEP_MEASURE} />
      </ul>
    </Panel>
  )
}

export function SetupScreen({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const work = toWork(state)
  const offsetCode = state.offsets[state.activeOffset].code
  const manual = state.mode === 'JOG' || state.mode === 'MPG'

  return (
    <div className="flex h-full min-h-0 flex-col gap-1.5">
      <Panel
        title="Координаты"
        icon={Move}
        className="min-h-0 flex-1"
        right={<span className="font-mono text-xs font-semibold text-foreground">{offsetCode} · мм</span>}
      >
        <div className="flex h-full min-h-0 flex-col">
          {AXES.map((ax) => {
            const selected = manual && state.jogAxis === ax
            return (
              <div
                key={ax}
                aria-selected={selected}
                className={cn(
                  'relative flex min-h-0 flex-1 items-center gap-4 border-b px-3 last:border-b-0',
                  selected && 'bg-accent',
                )}
              >
                {selected && <span className="absolute inset-y-0 left-0 w-1 bg-primary" aria-hidden="true" />}
                <span
                  className={cn(
                    'w-8 shrink-0 font-mono text-2xl font-bold',
                    selected ? 'text-primary' : 'text-foreground',
                  )}
                >
                  {ax}
                </span>
                <span className="font-mono text-5xl font-bold tabular-nums leading-none tracking-tight">
                  {formatCoord(work[ax])}
                  {ax === 'A' && <span className="ml-1 font-sans text-xl text-muted-foreground">°</span>}
                </span>
                <div className="ml-auto text-right leading-tight">
                  <p className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">Машинные</p>
                  <p className="font-mono text-2xl font-bold tabular-nums text-muted-foreground">
                    {formatCoord(state.machine[ax])}
                  </p>
                </div>
              </div>
            )
          })}
        </div>
      </Panel>

      <div className="flex shrink-0 gap-1.5">
        <MdiInput state={state} dispatch={dispatch} className="min-w-0 flex-1" />
        <ProbeSensors state={state} />
      </div>
    </div>
  )
}

export function SetupSide({
  state,
  dispatch,
  showPad = true,
  wide = false,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  showPad?: boolean
  wide?: boolean
}) {
  return (
    <aside className="flex min-h-0 flex-col gap-2" aria-label="Наладка: состояние">
      <Panel title="Наладка" className="min-h-0 flex-1">
        <div className={wide ? WIDE_SECTIONS : 'flex h-full flex-col overflow-y-auto'}>
          <ToolSection state={state} className={wide ? 'border-r' : undefined} />

          <ActiveCodes state={state} area="codes" className={wide ? 'border-r' : undefined} />

          <Section icon={SlidersHorizontal} title="Корректоры" area="correctors">
            <OverrideRow
              label="S%"
              value={state.spindleOverride}
              max={paramValue(state.params, MAX_SPINDLE_PARAM)}
              keys={['[', ']']}
            />
            <OverrideRow
              label="F%"
              value={state.feedOverride}
              max={paramValue(state.params, MAX_FEED_PARAM)}
              keys={['−', '=']}
            />
            <RapidRow value={state.rapidOverride} />
          </Section>
        </div>
      </Panel>

      {showPad && <OperatorPad state={state} dispatch={dispatch} layout="compact" />}
    </aside>
  )
}
