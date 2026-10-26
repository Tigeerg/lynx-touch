import {
  Disc3,
  Droplets,
  Fan,
  FileCode,
  Gauge,
  Hand,
  History,
  OctagonX,
  Package,
  Pause,
  Play,
  RotateCcw,
  Timer,
  Wrench,
  type LucideIcon,
} from 'lucide-react'
import type { ReactNode } from 'react'
import { MAX_RPM, RAPID_STEPS } from '@/lib/cnc/data'
import { motionSpeed, programLines, type Action } from '@/lib/cnc/machine'
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM, paramValue } from '@/lib/cnc/service-data'
import { spindleLoad, spindleRpm } from '@/lib/cnc/spindle'
import { JOG_FEEDS, MPG_STEPS, mpgMultiplier, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatTime, Keycap, Lamp, OverrideBar, Panel } from './primitives'

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
}: {
  icon: LucideIcon
  title: string
  right?: ReactNode
  children: ReactNode
}) {
  return (
    <section className="flex flex-col gap-0.5 border-b px-2.5 py-0.5 last:border-b-0">
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

function Reading({ value, unit, set, setLabel }: { value: number; unit: string; set: number; setLabel: string }) {
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

export function OverrideRow({
  label,
  value,
  max,
  keys,
}: {
  label: string
  value: number
  max: number
  keys: [string, string]
}) {
  return (
    <div className="flex items-center gap-2 text-xs">
      <span className="w-6 font-semibold text-muted-foreground">{label}</span>
      <Keycap className="h-5 min-w-5 px-1 text-[10px]">{keys[0]}</Keycap>
      <div className="flex-1">
        <OverrideBar value={value} max={max} />
      </div>
      <Keycap className="h-5 min-w-5 px-1 text-[10px]">{keys[1]}</Keycap>
      <span className="w-10 text-right font-mono text-sm font-bold tabular-nums">{value}%</span>
    </div>
  )
}

export function LoadRow({ value }: { value: number }) {
  const tone = value > 80 ? 'bg-destructive' : value > 60 ? 'bg-warning' : 'bg-primary'
  return (
    <div className="flex items-center gap-2 text-xs">
      <span className="w-6 font-semibold text-muted-foreground">Нгр</span>
      <div
        role="meter"
        aria-label="Нагрузка на шпиндель"
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.min(100, value)}
        className="relative h-3 flex-1 overflow-hidden rounded-sm border bg-muted"
      >
        <div
          className={cn('h-full transition-[width,background-color] duration-300', tone)}
          style={{ width: `${Math.min(100, value)}%` }}
        />
        <span className="absolute inset-y-0 left-[60%] w-px bg-foreground/30" aria-hidden="true" />
        <span className="absolute inset-y-0 left-[80%] w-px bg-foreground/30" aria-hidden="true" />
      </div>
      <span
        className={cn('w-10 text-right font-mono text-sm font-bold tabular-nums', value > 80 && 'text-destructive')}
      >
        {value}%
      </span>
    </div>
  )
}

export function RapidRow({ value }: { value: number }) {
  return (
    <div className="flex items-center gap-2 text-xs">
      <span className="w-6 font-semibold text-muted-foreground">G0</span>
      <Keycap className="h-5 min-w-5 px-1 text-[10px]">9</Keycap>
      <div className="flex flex-1 overflow-hidden rounded-sm border">
        {RAPID_STEPS.map((r) => (
          <span
            key={r}
            className={cn(
              'flex-1 py-0.5 text-center font-mono text-xs font-bold',
              r === value ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
            )}
          >
            {r}
          </span>
        ))}
      </div>
      <Keycap className="h-5 min-w-5 px-1 text-[10px]">0</Keycap>
    </div>
  )
}

export const WIDE_SECTIONS =
  'grid h-full grid-cols-2 content-start overflow-y-auto [&>section:nth-child(odd)]:border-r [&>section]:py-1.5'

export function SideStatus({
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
  const rpm = spindleRpm(state)
  const load = spindleLoad(state)
  const feed = state.run === 'RUN' ? Math.round(motionSpeed(state)) : 0
  const tool = state.tools.find((x) => x.number === state.tool)
  const program = programLines(state)
  const progress = Math.round((state.line / Math.max(1, program.length - 1)) * 100)

  return (
    <aside className="flex min-h-0 flex-col gap-2" aria-label="Состояние станка">
      <Panel title="Режимы" className="min-h-0 flex-1">
        <div className={wide ? WIDE_SECTIONS : 'flex h-full flex-col overflow-y-auto'}>
          <Section
            icon={FileCode}
            title="Кадр"
            right={<span className="font-mono font-bold tabular-nums">N{String(state.line + 1).padStart(4, '0')}</span>}
          >
            <p className="truncate rounded-sm bg-primary px-2 py-0.5 font-mono text-xs font-bold text-primary-foreground">
              {program[state.line]}
            </p>
            <div className="flex items-center gap-2 text-xs">
              <div className="h-2 flex-1 overflow-hidden rounded-sm border bg-muted">
                <div className="h-full bg-primary transition-[width]" style={{ width: `${progress}%` }} />
              </div>
              <span className="w-10 text-right font-mono text-xs font-bold tabular-nums">{progress}%</span>
            </div>
            <dl className="grid grid-cols-3 gap-1 border-t pt-0.5">
              <ProdStat icon={Timer} label="Цикл" value={formatTime(state.cycleTime)} />
              <ProdStat icon={History} label="Прошлый" value={formatTime(state.lastCycle)} />
              <ProdStat icon={Package} label="Деталей" value={String(state.partCount)} />
            </dl>
          </Section>

          <Section
            icon={Fan}
            title="Шпиндель"
            right={
              <>
                <Lamp on={rpm > 0} /> {rpm > 0 ? 'M03' : 'M05'}
              </>
            }
          >
            <Reading value={rpm} unit="об/мин" set={state.spindleSet} setLabel="S" />
            <OverrideRow
              label="S%"
              value={state.spindleOverride}
              max={paramValue(state.params, MAX_SPINDLE_PARAM)}
              keys={['[', ']']}
            />
            <LoadRow value={load} />
            <p className="sr-only">Максимум {MAX_RPM} об/мин</p>
          </Section>

          <Section icon={Gauge} title="Подача">
            <Reading value={feed} unit="мм/мин" set={state.programFeed} setLabel="F" />
            <OverrideRow
              label="F%"
              value={state.feedOverride}
              max={paramValue(state.params, MAX_FEED_PARAM)}
              keys={['−', '=']}
            />
            <RapidRow value={state.rapidOverride} />
          </Section>

          <Section
            icon={Wrench}
            title="Инструмент"
            right={
              <>
                <Droplets
                  className={cn('size-3.5', state.coolant ? 'text-primary' : 'text-muted-foreground/50')}
                  aria-hidden="true"
                />
                СОЖ {state.coolant ? 'вкл' : 'выкл'}
              </>
            }
          >
            <div className="flex items-center gap-2.5">
              <span className="flex h-7 w-10 shrink-0 items-center justify-center rounded-sm bg-primary font-mono text-sm font-bold text-primary-foreground">
                T{String(state.tool).padStart(2, '0')}
              </span>
              <div className="min-w-0 leading-tight">
                <p className="truncate text-sm font-semibold">{tool?.name}</p>
                <p className="truncate font-mono text-xs text-muted-foreground">
                  D{tool?.diameter} · L {tool?.length.toFixed(3)} · H{String(state.tool).padStart(2, '0')}
                </p>
              </div>
            </div>
          </Section>

          {state.mode === 'JOG' && <JogSection state={state} dispatch={dispatch} />}
          {state.mode === 'MPG' && <MpgSection state={state} />}
        </div>
      </Panel>

      {showPad && <OperatorPad state={state} dispatch={dispatch} layout="compact" />}
    </aside>
  )
}

function JogSection({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  return (
    <Section
      icon={Hand}
      title="Ручная подача"
      right={
        <span className="font-mono font-bold">
          Ось {state.jogAxis} · шаг {state.jogStep}
        </span>
      }
    >
      <div role="radiogroup" aria-label="Скорость ручной подачи, мм/мин" className="grid grid-cols-4 gap-1">
        {JOG_FEEDS.map((f) => (
          <button
            key={f}
            type="button"
            tabIndex={-1}
            role="radio"
            aria-checked={state.jogFeed === f}
            onClick={() => dispatch({ type: 'SET_JOG_FEED', feed: f })}
            className={cn(
              'h-7 rounded-sm border font-mono text-xs font-bold tabular-nums transition-colors',
              state.jogFeed === f
                ? 'border-primary bg-primary text-primary-foreground'
                : 'bg-background hover:bg-accent',
            )}
          >
            {f}
          </button>
        ))}
      </div>
    </Section>
  )
}

function MpgSection({ state }: { state: MachineState }) {
  return (
    <Section icon={Disc3} title="Маховик" right={<span className="font-mono font-bold">Ось {state.jogAxis}</span>}>
      <div role="status" aria-label="Множитель шага маховика" className="grid grid-cols-3 gap-1">
        {MPG_STEPS.map((step) => {
          const active = state.jogStep === step
          return (
            <div
              key={step}
              className={cn(
                'flex flex-col items-center rounded-sm border py-0.5 transition-colors',
                active ? 'border-primary bg-primary text-primary-foreground' : 'bg-background text-muted-foreground',
              )}
            >
              <span className="font-mono text-lg font-bold leading-tight tabular-nums">×{mpgMultiplier(step)}</span>
              <span className="font-mono text-[10px] font-semibold leading-tight">{step} мм</span>
            </div>
          )
        })}
      </div>
    </Section>
  )
}

type PadItem = {
  label: string
  short: string
  keyLabel: string
  icon: LucideIcon
  lit: boolean
  /** The face of the key while the state is off: a pale tint of the key's own colour. */
  cap: string
  /** The face of the key while the state is on: the same colour, saturated. */
  tone: string
  action: Action
}

export function OperatorPad({
  state,
  dispatch,
  layout = 'grid',
}: {
  state: MachineState
  dispatch: (a: Action) => void
  layout?: 'grid' | 'row' | 'compact'
}) {
  const items: PadItem[] = [
    {
      label: 'Пуск цикла',
      short: 'Пуск',
      keyLabel: 'Enter',
      icon: Play,
      lit: state.run === 'RUN',
      cap: 'border-hw-start bg-hw-start text-hw-ink',
      tone: 'border-success bg-success text-white',
      action: { type: 'CYCLE_START' },
    },
    {
      label: 'Стоп подачи',
      short: 'Стоп',
      keyLabel: 'Space',
      icon: Pause,
      lit: state.run === 'HOLD',
      cap: 'border-hw-stop bg-hw-stop text-hw-ink',
      tone: 'border-warning bg-warning text-warning-foreground',
      action: { type: 'FEED_HOLD' },
    },
    {
      label: 'Сброс',
      short: 'Сброс',
      keyLabel: 'Esc',
      icon: RotateCcw,
      lit: state.run === 'ALARM',
      cap: 'border-hw-reset bg-hw-reset text-hw-ink',
      tone: 'border-primary bg-primary text-primary-foreground',
      action: { type: 'RESET' },
    },
    {
      label: 'Авар. стоп',
      short: 'Авария',
      keyLabel: 'Shift+Esc',
      icon: OctagonX,
      lit: state.run === 'ESTOP',
      cap: 'border-destructive bg-destructive/15 text-destructive',
      tone: 'border-destructive bg-destructive text-white',
      action: { type: 'ESTOP' },
    },
  ]

  const compact = layout === 'compact'
  const buttons = items.map((item) => (
    <OperatorButton key={item.keyLabel} item={item} compact={compact} onClick={() => dispatch(item.action)} />
  ))

  if (compact) {
    // One gutter for the whole pad: the same 6 px between the keys and to the edges of the block,
    // as in the framed layout below.
    return (
      <div className="grid shrink-0 grid-cols-4 gap-1.5 p-1.5" role="group" aria-label="Пульт оператора">
        {buttons}
      </div>
    )
  }

  return (
    <Panel title="Пульт оператора" className="shrink-0">
      <div className={cn('grid gap-1.5 p-1.5', layout === 'row' ? 'grid-cols-4' : 'grid-cols-2')}>{buttons}</div>
    </Panel>
  )
}

function OperatorButton({ item, compact, onClick }: { item: PadItem; compact: boolean; onClick: () => void }) {
  const Icon = item.icon
  return (
    <button
      type="button"
      tabIndex={-1}
      onClick={onClick}
      aria-pressed={item.lit}
      title={`${item.label} (${item.keyLabel})`}
      className={cn(
        'rounded-md border-2 transition-colors',
        compact
          ? 'flex h-10 flex-col items-center justify-center gap-0.5 px-2'
          : 'flex h-11 items-center gap-2 px-2.5 text-left',
        item.lit ? item.tone : item.cap,
      )}
    >
      <Icon className={cn('shrink-0', compact ? 'size-4' : 'size-5')} aria-hidden="true" />
      {compact ? (
        <span className="text-[10px] font-bold leading-none">{item.short}</span>
      ) : (
        <>
          <span className="truncate text-xs font-bold leading-none">{item.label}</span>
          <Keycap className="ml-auto h-5 text-[10px] text-foreground/80">{item.keyLabel}</Keycap>
        </>
      )}
    </button>
  )
}
