import { motionSpeed, programLines, type Action } from '@/lib/cnc/machine'
import { MAX_RPM } from '@/lib/cnc/data'
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM, paramValue } from '@/lib/cnc/service-data'
import { spindleLoad, spindleRpm } from '@/lib/cnc/spindle'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Droplets, Fan, FileCode, Gauge, History, Package, Timer, Wrench } from 'lucide-react'
import { Lamp, Panel, formatTime } from '../primitives'
import { ProdStat, Reading, SIDE_FLOW, Section } from './sections'
import { OverrideRow } from './tool'
import { LoadRow, RapidRow } from './load'
import { JogSection, MpgSection } from './jog'
import { OperatorPad } from './operator-pad'

export function SideStatus({
  state,
  dispatch,
  showPad = true,
  wide = false,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  showPad?: boolean
  wide?: boolean
  className?: string
}) {
  const rpm = spindleRpm(state)
  const load = spindleLoad(state)
  const feed = state.run === 'RUN' ? Math.round(motionSpeed(state)) : 0
  const tool = state.tools.find((x) => x.number === state.tool)
  const program = programLines(state)
  const progress = Math.round((state.line / Math.max(1, program.length - 1)) * 100)

  return (
    <aside className={cn('flex min-h-0 flex-col gap-2', className)} aria-label="Состояние станка">
      <Panel title="Режимы" className="min-h-0 flex-1">
        <div className={wide ? SIDE_FLOW : 'flex h-full flex-col overflow-y-auto'}>
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
