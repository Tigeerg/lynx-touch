import { Braces, Fan, FileCode, History, Package, Timer } from 'lucide-react'
import { modalCodes } from '@/lib/cnc/gcode'
import { programLines, type Action } from '@/lib/cnc/machine'
import { axisLoad, spindleLoad } from '@/lib/cnc/spindle'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatTime, Panel } from './primitives'
import {
  AxisLoad,
  CorrectorsSection,
  LoadRow,
  OperatorPad,
  ProdStat,
  PANEL_SECTIONS,
  Section,
  ToolSection,
  TOUCH_SECTIONS,
  TOUCH_SECTIONS_NO_EXEC,
} from './side-status'

const g = (n: number) => `G${String(n).padStart(2, '0')}`

const WORK_MOTION = new Set(['G01', 'G02', 'G03'])
const WORK_OFFSET = /^G5[4-9]$/

// The one name of the execution block: the side column and the separate touch AUTO panel show
// the same words (CLAUDE.md, section 5: a word needed in two places has one source).
const EXECUTION_TITLE = 'Выполнение'

// Calm tones from the theme tokens: amber is attention, muted green is "safe working", neutral
// stays for plane/units/corrections. M-codes are a running state rather than a deviation, so they
// keep the neutral tone even when the spindle or coolant is on.
function chipTone(value: string, hot: boolean) {
  if (value === 'G00') return 'border-warning/70 bg-warning/30 text-foreground'
  if (WORK_MOTION.has(value) || WORK_OFFSET.test(value)) return 'border-success/40 bg-success/15 text-foreground'
  if (hot && !value.startsWith('M')) return 'border-warning/45 bg-warning/15 text-foreground'
  return 'bg-muted text-muted-foreground'
}

function Chip({ label, value, hot }: { label: string; value: string; hot: boolean }) {
  return (
    <li
      title={label}
      className={cn(
        'rounded-sm border py-0.5 text-center font-mono text-xs font-bold tabular-nums',
        chipTone(value, hot),
      )}
    >
      <span className="sr-only">{label}: </span>
      {value}
    </li>
  )
}

// "Active codes" - the modal G/M of the current block. A shared block for the program screen
// and the right column of Setup.
export function ActiveCodes({
  state,
  area,
  className,
}: {
  state: MachineState
  /** Named zone in the fixed side grid (WIDE_SECTIONS); the SETUP and AUTO sides use "codes". */
  area?: string
  className?: string
}) {
  const lines = programLines(state)
  const started = state.run !== 'IDLE' || state.line > 0
  const modal = modalCodes(lines, started ? state.line : -1)
  const codes = [
    { label: 'Движение', value: g(modal.motion), hot: modal.motion !== 0 },
    { label: 'Плоскость', value: g(modal.plane), hot: false },
    { label: 'Единицы', value: g(modal.unit), hot: modal.unit === 20 },
    { label: 'Координаты', value: g(modal.distance), hot: modal.distance === 91 },
    { label: 'Система координат', value: state.offsets[state.activeOffset].code, hot: false },
    { label: 'Коррекция радиуса', value: g(modal.radius), hot: modal.radius !== 40 },
    { label: 'Коррекция длины', value: g(modal.length), hot: modal.length !== 49 },
    { label: 'Цикл', value: g(modal.cycle), hot: modal.cycle !== 80 },
    { label: 'Шпиндель', value: state.spindleOn ? 'M03' : 'M05', hot: state.spindleOn },
    { label: 'СОЖ', value: state.coolant ? 'M08' : 'M09', hot: state.coolant },
  ]
  return (
    <Section icon={Braces} title="Активные коды" area={area} className={className}>
      <ul className="grid grid-cols-5 gap-1" aria-label="Активные G и M коды">
        {codes.map((c) => (
          <Chip key={c.label} {...c} />
        ))}
      </ul>
    </Section>
  )
}

// The counter in the block header: the block in execution and the length of the program.
function ExecutionCount({ state }: { state: MachineState }) {
  return (
    <span className="font-mono font-bold tabular-nums">
      {state.line + 1}
      <span className="text-muted-foreground">/{programLines(state).length}</span>
    </span>
  )
}

// The body of the "Execution" block: the run progress, the block after the current one and the
// cycle counters. Shared by the "Machining" column and the separate panel of the touch AUTO
// screen; both read the same numbers and words.
export function ExecutionBody({ state }: { state: MachineState }) {
  const lines = programLines(state)
  const progress = Math.round((state.line / Math.max(1, lines.length - 1)) * 100)
  const nextLine = lines[state.nextLine]

  return (
    <>
      <div className="flex items-center gap-2 text-xs">
        <div className="h-4 flex-1 overflow-hidden rounded-sm border bg-muted">
          <div className="progress-zebra h-full transition-[width]" style={{ width: `${progress}%` }} />
        </div>
        <span className="w-10 text-right font-mono text-sm font-bold tabular-nums">{progress}%</span>
      </div>
      <p className="flex min-w-0 items-baseline gap-1.5 text-xs">
        <span className="shrink-0 font-semibold uppercase tracking-wider text-muted-foreground">Далее</span>
        <span className="truncate font-mono font-bold">{nextLine ?? 'конец программы'}</span>
      </p>
      <dl className="grid grid-cols-3 gap-1 border-t pt-0.5">
        <ProdStat icon={Timer} label="Цикл" value={formatTime(state.cycleTime)} />
        <ProdStat icon={History} label="Прошлый" value={formatTime(state.lastCycle)} />
        <ProdStat icon={Package} label="Деталей" value={String(state.partCount)} />
      </dl>
    </>
  )
}

// The same block as a panel of its own: the touch AUTO screen moves it out of "Machining" so the
// run progress sits between the program and the machining column instead of inside it.
export function ExecutionPanel({ state, className }: { state: MachineState; className?: string }) {
  return (
    <Panel
      title={EXECUTION_TITLE}
      icon={FileCode}
      className={cn('shrink-0', className)}
      right={<ExecutionCount state={state} />}
    >
      <div className="flex flex-col gap-0.5 px-2.5 py-1.5">
        <ExecutionBody state={state} />
      </div>
    </Panel>
  )
}

export function ProgramSide({
  state,
  dispatch,
  showPad = true,
  wide = false,
  showExecution = true,
  touch = false,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  showPad?: boolean
  wide?: boolean
  /** The execution block lives inside "Machining"; the touch AUTO screen shows it separately. */
  showExecution?: boolean
  /** Touch panel: the correctors get finger-sized buttons instead of the physical-key caps. */
  touch?: boolean
  className?: string
}) {
  const load = spindleLoad(state)

  return (
    <aside className={cn('flex min-h-0 flex-col gap-2', className)} aria-label="Обработка программы">
      <section
        className={cn('cnc-box flex min-h-0 flex-1 flex-col overflow-hidden', !wide && 'rounded-md border bg-card')}
      >
        <div className={wide ? (showExecution ? TOUCH_SECTIONS : TOUCH_SECTIONS_NO_EXEC) : PANEL_SECTIONS}>
          <ToolSection state={state} />

          <CorrectorsSection state={state} dispatch={dispatch} touch={touch} />

          <ActiveCodes state={state} area="codes" />

          {/* The panel column has five blocks in one column, so the four axes sit in two columns:
              the same readings, half the height, and nothing needs a scrollbar. */}
          <Section icon={Fan} title="Нагрузка (имитация)" area="load">
            <LoadRow value={load} />
            <div className={wide ? 'flex flex-col gap-0.5' : 'grid grid-cols-2 gap-x-2 gap-y-0.5'}>
              {AXES.map((ax) => (
                <AxisLoad key={ax} axis={ax} value={axisLoad(state, ax)} />
              ))}
            </div>
          </Section>

          {showExecution && (
            <Section icon={FileCode} title={EXECUTION_TITLE} area="execution" right={<ExecutionCount state={state} />}>
              <ExecutionBody state={state} />
            </Section>
          )}
        </div>
      </section>

      {showPad && <OperatorPad state={state} dispatch={dispatch} layout="compact" />}
    </aside>
  )
}
