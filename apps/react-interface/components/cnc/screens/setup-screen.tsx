import { Move, Radio } from 'lucide-react'
import { toWork, type Action } from '@/lib/cnc/machine'
import { PROBE_STEP_MEASURE, PROBE_TOOL } from '@/lib/cnc/probe'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { MdiInput } from '../mdi-input'
import { ActiveCodes } from '../program-side'
import { formatDro, Lamp, Panel } from '../primitives'
import {
  CorrectorsSection,
  OperatorPad,
  PANEL_SECTIONS,
  Section,
  ToolSection,
  TOUCH_SECTIONS_NO_EXEC,
} from '../side-status'

// The SETUP workplace: on the left the large work and machine coordinates and the MDI line;
// on the right the tool, the overrides, the active codes and the probe sensors. The toolpath
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

export function ProbeSensors({ state, className }: { state: MachineState; className?: string }) {
  return (
    <Section icon={Radio} title="Датчики щупа" area="load" className={className}>
      <ul className="flex flex-col gap-1 p-1 text-xs">
        <Indicator label="Щуп T99 в шпинделе" on={state.tool === PROBE_TOOL} />
        <Indicator label="Измерение идёт" on={state.probe.running} />
        <Indicator label="Есть результат" on={state.probe.result !== null && state.probe.step >= PROBE_STEP_MEASURE} />
      </ul>
    </Section>
  )
}

export function SetupMdi({
  state,
  dispatch,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  className?: string
}) {
  return <MdiInput state={state} dispatch={dispatch} className={cn('h-full min-w-0 flex-1', className)} />
}

export function SetupScreen({
  state,
  dispatch,
  showMdi = true,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  /** The touch panel moves the MDI row into the fixed lower zone; the 1024 panel keeps it here. */
  showMdi?: boolean
}) {
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
                  {formatDro(work[ax])}
                  {ax === 'A' && <span className="ml-1 font-sans text-xl text-muted-foreground">°</span>}
                </span>
                <div className="ml-auto text-right leading-tight">
                  <p className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">Машинные</p>
                  <p className="font-mono text-2xl font-bold tabular-nums text-muted-foreground">
                    {formatDro(state.machine[ax])}
                  </p>
                </div>
              </div>
            )
          })}
        </div>
      </Panel>

      {showMdi && (
        <div className="flex shrink-0">
          <MdiInput state={state} dispatch={dispatch} className="min-w-0 flex-1" />
        </div>
      )}
    </div>
  )
}

export function SetupSide({
  state,
  dispatch,
  showPad = true,
  wide = false,
  touch = false,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  showPad?: boolean
  wide?: boolean
  /** Touch panel: the correctors get finger-sized buttons instead of the physical-key caps. */
  touch?: boolean
  className?: string
}) {
  return (
    <aside className={cn('flex min-h-0 flex-col gap-2', className)} aria-label="Наладка: состояние">
      <section
        className={cn('cnc-box flex min-h-0 flex-1 flex-col overflow-hidden', !wide && 'rounded-md border bg-card')}
      >
        <div className={wide ? TOUCH_SECTIONS_NO_EXEC : PANEL_SECTIONS}>
          <ToolSection state={state} />

          <CorrectorsSection state={state} dispatch={dispatch} touch={touch} />

          <ActiveCodes state={state} area="codes" />

          <ProbeSensors state={state} />
        </div>
      </section>

      {showPad && <OperatorPad state={state} dispatch={dispatch} layout="compact" />}
    </aside>
  )
}
