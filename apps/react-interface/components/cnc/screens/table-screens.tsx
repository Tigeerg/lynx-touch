import { CircleAlert, Crosshair, Info, TriangleAlert, Wrench } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatCoord, formatTime, Panel } from '../primitives'
import { ProbeWizard } from '../probe/probe-wizard'

const th = 'px-2.5 py-1 text-left text-[10px] font-semibold uppercase tracking-wider text-muted-foreground'
const rowClass = (cursor: boolean) =>
  cn('border-b last:border-b-0', cursor && 'bg-accent outline-2 -outline-offset-2 outline-primary')

export function OffsetScreen({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  if (state.probe.open) return <ProbeWizard state={state} />
  return (
    <Panel
      title="Рабочие смещения"
      icon={Crosshair}
      className="h-full"
      right={<span className="text-[11px] text-muted-foreground">Привязка записывает машинную координату</span>}
    >
      <div className="h-full overflow-y-auto">
        <table className="w-full border-collapse">
          <thead className="sticky top-0 bg-card">
            <tr className="border-b">
              <th className={th}>Код</th>
              {AXES.map((ax) => (
                <th key={ax} className={cn(th, 'text-right')}>
                  {ax}
                </th>
              ))}
              <th className={cn(th, 'text-right')}>Статус</th>
            </tr>
          </thead>
          <tbody>
            {state.offsets.map((o, i) => (
              <tr
                key={o.code}
                className={cn(rowClass(i === state.cursor.offset), 'cursor-pointer select-none hover:bg-accent/60')}
                aria-selected={i === state.cursor.offset}
                onClick={() => dispatch({ type: 'CURSOR', delta: i - state.cursor.offset })}
              >
                <td className="px-2.5 py-1.5 font-mono text-base font-bold">{o.code}</td>
                {AXES.map((ax) => (
                  <td key={ax} className="px-2.5 py-1.5 text-right font-mono text-base tabular-nums">
                    {formatCoord(o.values[ax])}
                  </td>
                ))}
                <td className="px-2.5 py-1.5 text-right">
                  {i === state.activeOffset && (
                    <span className="rounded-sm bg-primary px-2 py-0.5 text-xs font-bold text-primary-foreground">
                      АКТИВНО
                    </span>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
          <tfoot>
            <tr className="border-t-2 bg-muted">
              <td className="px-2.5 py-1 text-[10px] font-semibold uppercase text-muted-foreground">Машина</td>
              {AXES.map((ax) => (
                <td key={ax} className="px-2.5 py-1 text-right font-mono text-xs tabular-nums text-muted-foreground">
                  {formatCoord(state.machine[ax])}
                </td>
              ))}
              <td />
            </tr>
          </tfoot>
        </table>
      </div>
    </Panel>
  )
}

export function ToolScreen({ state }: { state: MachineState }) {
  return (
    <Panel title="Таблица инструмента" icon={Wrench} className="h-full">
      <div className="h-full overflow-y-auto">
        <table className="w-full table-fixed border-collapse">
          <thead className="sticky top-0 bg-card">
            <tr className="border-b">
              <th className={cn(th, 'w-16')}>T</th>
              <th className={th}>Наименование</th>
              <th className={cn(th, 'w-20 text-right')}>D, мм</th>
              <th className={cn(th, 'w-24 text-right')}>L, мм</th>
              <th className={cn(th, 'w-48')}>Ресурс</th>
              <th className={cn(th, 'w-24 text-right')}>Наработка</th>
            </tr>
          </thead>
          <tbody>
            {state.tools.map((t, i) => {
              const inSpindle = t.number === state.tool
              return (
                <tr
                  key={t.number}
                  className={rowClass(i === state.cursor.tool)}
                  aria-selected={i === state.cursor.tool}
                >
                  <td className="px-2.5 py-1">
                    <span
                      className={cn(
                        'inline-flex h-6 w-11 items-center justify-center rounded-sm font-mono text-sm font-bold',
                        inSpindle ? 'bg-primary text-primary-foreground' : 'bg-muted',
                      )}
                    >
                      T{String(t.number).padStart(2, '0')}
                    </span>
                  </td>
                  <td className="px-2.5 py-1">
                    <div className="flex min-w-0 items-center gap-2">
                      <span className="truncate text-sm font-medium" title={t.name}>
                        {t.name}
                      </span>
                      {inSpindle && <span className="shrink-0 text-xs font-semibold text-primary">в шпинделе</span>}
                    </div>
                  </td>
                  <td className="px-2.5 py-1 text-right font-mono text-sm tabular-nums">{t.diameter.toFixed(1)}</td>
                  <td className="px-2.5 py-1 text-right font-mono text-sm tabular-nums">{t.length.toFixed(3)}</td>
                  <td className="px-2.5 py-1">
                    <div className="flex items-center gap-2">
                      <div className="h-2.5 flex-1 overflow-hidden rounded-sm border bg-muted">
                        <div
                          className={cn(
                            'h-full',
                            t.life < 15 ? 'bg-destructive' : t.life < 40 ? 'bg-warning' : 'bg-success',
                          )}
                          style={{ width: `${t.life}%` }}
                        />
                      </div>
                      <span className="w-10 text-right font-mono text-sm font-semibold tabular-nums">{t.life}%</span>
                    </div>
                  </td>
                  <td className="px-2.5 py-1 text-right font-mono text-sm tabular-nums text-muted-foreground">
                    {formatTime(state.toolTime[t.number] ?? 0)}
                  </td>
                </tr>
              )
            })}
          </tbody>
        </table>
      </div>
    </Panel>
  )
}

const LEVEL = {
  error: { icon: CircleAlert, className: 'text-destructive', label: 'Авария' },
  warning: { icon: TriangleAlert, className: 'text-warning-foreground', label: 'Предупр.' },
  info: { icon: Info, className: 'text-primary', label: 'Инфо' },
}

export function AlarmScreen({ state }: { state: MachineState }) {
  return (
    <Panel title="Аварии и сообщения" icon={TriangleAlert} className="h-full">
      {state.alarms.length === 0 ? (
        <div className="flex h-full items-center justify-center p-6 text-sm text-muted-foreground">Сообщений нет</div>
      ) : (
        <ul className="h-full overflow-y-auto" aria-label="Список сообщений">
          {state.alarms.map((al, i) => {
            const lvl = LEVEL[al.level]
            const Icon = lvl.icon
            return (
              <li
                key={al.id}
                aria-selected={i === state.cursor.alarm}
                className={cn(
                  'flex items-center gap-3 border-b px-2.5 py-1.5',
                  i === state.cursor.alarm && 'bg-accent outline-2 -outline-offset-2 outline-primary',
                  !al.active && 'opacity-55',
                )}
              >
                <Icon className={cn('size-6 shrink-0', lvl.className)} aria-hidden="true" />
                <span className="w-14 shrink-0 font-mono text-base font-bold">{al.code}</span>
                <div className="min-w-0 flex-1">
                  <p className="truncate text-sm font-semibold">{al.text}</p>
                  <p className="text-xs text-muted-foreground">{lvl.label}</p>
                </div>
                <span className="font-mono text-sm tabular-nums text-muted-foreground">{al.time}</span>
                <span
                  className={cn(
                    'w-24 rounded-sm px-2 py-0.5 text-center text-xs font-bold',
                    al.active ? 'bg-foreground text-background' : 'border text-muted-foreground',
                  )}
                >
                  {al.active ? 'АКТИВНО' : 'КВИТИРОВАНО'}
                </span>
              </li>
            )
          })}
        </ul>
      )}
    </Panel>
  )
}
