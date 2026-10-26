import { Braces, Fan, FileCode, History, Package, SlidersHorizontal, Timer, Wrench } from 'lucide-react'
import { modalCodes, programTools } from '@/lib/cnc/gcode'
import { programLines, type Action } from '@/lib/cnc/machine'
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM, paramValue } from '@/lib/cnc/service-data'
import { spindleLoad } from '@/lib/cnc/spindle'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatTime, Panel } from './primitives'
import { LoadRow, OperatorPad, OverrideRow, ProdStat, RapidRow, Section, WIDE_SECTIONS } from './side-status'

const g = (n: number) => `G${String(n).padStart(2, '0')}`

function Chip({ label, value, hot }: { label: string; value: string; hot: boolean }) {
  return (
    <li
      title={label}
      className={cn(
        'rounded-sm border py-0.5 text-center font-mono text-xs font-bold tabular-nums',
        hot ? 'border-primary bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
      )}
    >
      <span className="sr-only">{label}: </span>
      {value}
    </li>
  )
}

// "Active codes" - the modal G/M of the current block. A shared block for the program screen
// and the right column of Setup.
export function ActiveCodes({ state }: { state: MachineState }) {
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
    <Section icon={Braces} title="Активные коды">
      <ul className="grid grid-cols-5 gap-1" aria-label="Активные G и M коды">
        {codes.map((c) => (
          <Chip key={c.label} {...c} />
        ))}
      </ul>
    </Section>
  )
}

export function ProgramSide({
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
  const lines = programLines(state)
  const progress = Math.round((state.line / Math.max(1, lines.length - 1)) * 100)
  const nextLine = lines[state.nextLine]
  const tools = programTools(lines)
  const toolList = tools.length > 0 ? tools : [state.tool]
  const load = spindleLoad(state)

  return (
    <aside className="flex min-h-0 flex-col gap-2" aria-label="Обработка программы">
      <Panel title="Обработка" className="min-h-0 flex-1">
        <div className={wide ? WIDE_SECTIONS : 'flex h-full flex-col overflow-y-auto'}>
          <Section
            icon={FileCode}
            title="Выполнение"
            right={
              <span className="font-mono font-bold tabular-nums">
                {state.line + 1}
                <span className="text-muted-foreground">/{lines.length}</span>
              </span>
            }
          >
            <div className="flex items-center gap-2 text-xs">
              <div className="h-2 flex-1 overflow-hidden rounded-sm border bg-muted">
                <div className="h-full bg-primary transition-[width]" style={{ width: `${progress}%` }} />
              </div>
              <span className="w-10 text-right font-mono text-xs font-bold tabular-nums">{progress}%</span>
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
          </Section>

          <ActiveCodes state={state} />

          <Section icon={Wrench} title="Инструмент программы">
            <ul className="flex flex-col gap-0.5">
              {toolList.slice(0, 3).map((n) => {
                const tool = state.tools.find((x) => x.number === n)
                const inSpindle = n === state.tool
                return (
                  <li
                    key={n}
                    className={cn('flex items-center gap-2 rounded-sm px-1 py-0.5', inSpindle && 'bg-accent')}
                  >
                    <span
                      className={cn(
                        'flex h-6 w-9 shrink-0 items-center justify-center rounded-sm font-mono text-xs font-bold',
                        inSpindle ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
                      )}
                    >
                      T{String(n).padStart(2, '0')}
                    </span>
                    <span className="min-w-0 flex-1 truncate text-xs font-semibold">
                      {tool?.name ?? 'Не найден в таблице'}
                      {inSpindle && <span className="sr-only"> (в шпинделе)</span>}
                    </span>
                    <span className="shrink-0 font-mono text-xs tabular-nums text-muted-foreground" title="Наработка">
                      {formatTime(state.toolTime[n] ?? 0)}
                    </span>
                    {tool && (
                      <span
                        className={cn(
                          'font-mono text-xs font-bold tabular-nums',
                          tool.life < 10 ? 'text-destructive' : 'text-muted-foreground',
                        )}
                        title="Остаточный ресурс"
                      >
                        {tool.life}%
                      </span>
                    )}
                  </li>
                )
              })}
            </ul>
          </Section>

          <Section icon={Fan} title="Нагрузка">
            <LoadRow value={load} />
          </Section>

          <Section icon={SlidersHorizontal} title="Корректоры">
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
