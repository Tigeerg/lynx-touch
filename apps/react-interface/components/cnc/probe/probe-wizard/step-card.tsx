import { Check } from 'lucide-react'
import {
  PROBE_BALL_R,
  PROBE_CLEARANCE,
  PROBE_FAST_FEED,
  PROBE_FEED,
  PROBE_STEP_MEASURE,
  PROBE_STEPS,
  PROBE_TOOL,
  type ProbeCycle,
} from '@/lib/cnc/probe'
import type { MachineState } from '@/lib/cnc/types'
import { formatCoord, Keycap } from '../../primitives'
import { Row, Spec } from './readout'

const TITLES = ['Выбор цикла', 'Подготовка щупа', 'Подвод к заготовке', 'Измерение', 'Результат']

// Right column of the wizard: the step title and its body.
export function StepCard({ state, cycle }: { state: MachineState; cycle: ProbeCycle }) {
  const p = state.probe
  return (
    <div className="flex min-h-0 flex-col bg-card">
      <div className="flex shrink-0 items-baseline justify-between gap-2 border-b px-3 py-2">
        <h3 className="text-base font-bold leading-tight text-balance">{TITLES[p.step]}</h3>
        <p className="shrink-0 font-mono text-[11px] font-semibold text-muted-foreground">
          {p.step + 1}/{PROBE_STEPS.length}
        </p>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto px-3 py-2.5">
        <StepBody state={state} cycle={cycle} />
      </div>
    </div>
  )
}

function StepBody({ state, cycle }: { state: MachineState; cycle: ProbeCycle }) {
  const p = state.probe
  const target = state.offsets[state.cursor.offset]
  switch (p.step) {
    case 0:
      return (
        <div className="flex flex-col gap-2.5">
          <div>
            <p className="text-sm font-bold">{cycle.name}</p>
            <p className="mt-0.5 text-[13px] leading-snug text-pretty text-muted-foreground">{cycle.short}</p>
          </div>
          <Spec title="Параметры цикла">
            <Row label="Измеряемые оси" value={cycle.axes.join(' ')} />
            <Row label="Касаний" value={String(cycle.touches.length)} />
            <Row label="Запись результата" value={`${target.code} ${cycle.axes.join(' ')}`} />
          </Spec>
        </div>
      )
    case 1: {
      const tool = state.tools.find((t) => t.number === PROBE_TOOL)
      return (
        <div className="flex flex-col gap-2.5">
          <p className="text-[13px] leading-snug text-pretty text-muted-foreground">
            Установите щуп в шпиндель и убедитесь, что он включён.
          </p>
          <Spec title="Проверка готовности">
            <Row label="Щуп T99 в шпинделе" ok={state.tool === PROBE_TOOL} />
            <Row label="Шпиндель остановлен" ok={!state.spindleOn} />
            <Row label="Щуп откалиброван" ok />
            <Row label="Сигнал OMP40 · батарея" ok />
          </Spec>
          <Spec title="Геометрия щупа">
            <Row label="Ø шарика" value={`${(PROBE_BALL_R * 2).toFixed(3)} мм`} />
            <Row label="Вылет L" value={`${tool?.length.toFixed(3) ?? '—'} мм`} />
          </Spec>
        </div>
      )
    }
    case 2:
      return (
        <div className="flex flex-col gap-2.5">
          <p className="rounded-md border border-primary/40 bg-accent px-2.5 py-1.5 text-[13px] font-medium leading-snug text-pretty">
            {cycle.startHint}
          </p>
          <Spec title="Состояние">
            <Row label="Режим РУЧНОЙ / МАХОВИК" ok={state.mode === 'JOG' || state.mode === 'MPG'} />
            {(['X', 'Y', 'Z'] as const).map((ax) => (
              <Row key={ax} label={`Машина ${ax}`} value={formatCoord(state.machine[ax])} />
            ))}
          </Spec>
        </div>
      )
    case PROBE_STEP_MEASURE:
      return (
        <div className="flex flex-col gap-2.5">
          <Spec title="Режим измерения">
            <Row label="Поиск G38.2" value={`F${PROBE_FAST_FEED}`} />
            <Row label="Замер G38.4" value={`F${PROBE_FEED}`} />
            <Row label="Отскок" value={`${PROBE_CLEARANCE} мм`} />
            <Row label="Коррекция подачи" value={`${state.feedOverride}%`} />
          </Spec>
          {p.running ? (
            <p className="rounded-md bg-warning px-2 py-2.5 text-center text-sm font-bold text-warning-foreground">
              Идёт измерение…
            </p>
          ) : (
            <p className="flex items-center justify-center gap-2 rounded-md border-2 border-success bg-success/10 px-2 py-2.5 text-sm font-bold">
              Нажмите <Keycap>ПУСК</Keycap> на пульте
            </p>
          )}
        </div>
      )
    default: {
      const r = p.result
      if (!r) return null
      return (
        <div className="flex flex-col gap-2.5">
          <div className="overflow-hidden rounded-md border bg-background">
            <table className="w-full text-[13px]">
              <thead>
                <tr className="border-b bg-muted text-[10px] uppercase tracking-[0.14em] text-muted-foreground">
                  <th className="px-2.5 py-0.5 text-left font-bold">Ось</th>
                  <th className="px-2.5 py-0.5 text-right font-bold">Было</th>
                  <th className="px-2.5 py-0.5 text-right font-bold">Новое</th>
                </tr>
              </thead>
              <tbody>
                {Object.entries(r.values).map(([ax, v]) => (
                  <tr key={ax} className="border-b last:border-b-0">
                    <td className="px-2.5 py-1 font-mono font-bold">{ax}</td>
                    <td className="px-2.5 py-1 text-right font-mono tabular-nums text-muted-foreground">
                      {formatCoord((p.prev ?? target.values)[ax as 'X'] ?? 0)}
                    </td>
                    <td className="px-2.5 py-1 text-right font-mono font-bold tabular-nums text-primary">
                      {formatCoord(v as number)}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <Spec title="Данные измерения">
            {r.info
              .filter((l) => !l.label.startsWith('Контакт'))
              .map((l) => (
                <Row key={l.label} label={l.label} value={l.value} />
              ))}
            <Row label="Радиус шарика учтён" value={`${PROBE_BALL_R.toFixed(3)} мм`} />
          </Spec>
          {p.written && (
            <p className="flex items-center gap-2 rounded-md bg-success px-2.5 py-2 text-sm font-bold text-primary-foreground">
              <Check className="size-4" aria-hidden="true" /> Записано в {target.code}
            </p>
          )}
        </div>
      )
    }
  }
}
