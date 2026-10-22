import { Fan, Move, Radio, SlidersHorizontal, Wrench } from 'lucide-react'
import { toWork, type Action } from '@/lib/cnc/machine'
import { PROBE_STEP_MEASURE, PROBE_TOOL } from '@/lib/cnc/probe'
import { AXES, type Axis, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { MdiInput } from '../mdi-input'
import { ActiveCodes } from '../program-side'
import { formatCoord, formatTime, Lamp, Panel } from '../primitives'
import { LoadRow, OperatorPad, OverrideRow, RapidRow, Section } from '../side-status'

// The SETUP workplace: on the left the large work and machine coordinates, the MDI line and
// the probe indication; on the right the tool, the load, the active codes, the overrides. The
// toolpath graphics are hidden here: setup goes by the numbers.

function AxisLoad({ axis, value }: { axis: Axis; value: number }) {
  const tone = value > 80 ? 'bg-destructive' : value > 60 ? 'bg-warning' : 'bg-primary'
  return (
    <div className="flex items-center gap-2 text-xs">
      <span className="w-4 font-mono font-bold text-muted-foreground">{axis}</span>
      <div
        role="meter"
        aria-label={`Нагрузка оси ${axis}`}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.min(100, value)}
        className="relative h-2.5 flex-1 overflow-hidden rounded-sm border bg-muted"
      >
        <div
          className={cn('h-full transition-[width] duration-300', tone)}
          style={{ width: `${Math.min(100, value)}%` }}
        />
      </div>
      <span className="w-9 text-right font-mono text-xs font-bold tabular-nums">{value}%</span>
    </div>
  )
}

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
}: {
  state: MachineState
  dispatch: (a: Action) => void
  showPad?: boolean
}) {
  const tool = state.tools.find((x) => x.number === state.tool)
  const rpm =
    state.spindleOn && state.run !== 'ESTOP' ? Math.round((state.spindleSet * state.spindleOverride) / 100) : 0
  const cutting = state.run === 'RUN' && state.motion !== null && !state.motion.rapid
  const t = state.cycleTime / 400
  const load = rpm === 0 ? 0 : cutting ? Math.round(38 + Math.sin(t) * 9 + Math.sin(t * 2.7) * 5) : 6
  const axisLoad = (ax: Axis): number => {
    if (!cutting || !state.motion) return 0
    const d = Math.abs(state.motion.target[ax] - state.machine[ax])
    return d > 0.0005 ? Math.min(100, Math.round(20 + (d / 5) * 60)) : 4
  }

  return (
    <aside className="flex min-h-0 flex-col gap-2" aria-label="Наладка: состояние">
      <Panel title="Наладка" className="min-h-0 flex-1">
        <div className="flex h-full flex-col overflow-y-auto">
          <Section
            icon={Wrench}
            title="Инструмент"
            right={<span className="font-mono font-bold">T{String(state.tool).padStart(2, '0')}</span>}
          >
            <div className="flex items-center gap-2.5 pb-0.5">
              <span className="flex h-7 w-10 shrink-0 items-center justify-center rounded-sm bg-primary font-mono text-sm font-bold text-primary-foreground">
                T{String(state.tool).padStart(2, '0')}
              </span>
              <div className="min-w-0 leading-tight">
                <p className="truncate text-sm font-semibold">{tool?.name ?? 'Не найден в таблице'}</p>
                <p className="truncate font-mono text-xs text-muted-foreground">
                  D{tool?.diameter} · L {tool?.length.toFixed(3)} · ресурс {tool?.life}% · наработка{' '}
                  {formatTime(state.toolTime[state.tool] ?? 0)}
                </p>
              </div>
            </div>
          </Section>

          <Section icon={Fan} title="Нагрузка">
            <LoadRow value={load} />
            {AXES.map((ax) => (
              <AxisLoad key={ax} axis={ax} value={axisLoad(ax)} />
            ))}
          </Section>

          <ActiveCodes state={state} />

          <Section icon={SlidersHorizontal} title="Корректоры">
            <OverrideRow label="S%" value={state.spindleOverride} max={120} keys={['[', ']']} />
            <OverrideRow label="F%" value={state.feedOverride} max={150} keys={['−', '=']} />
            <RapidRow value={state.rapidOverride} />
          </Section>
        </div>
      </Panel>

      {showPad && <OperatorPad state={state} dispatch={dispatch} layout="compact" />}
    </aside>
  )
}
