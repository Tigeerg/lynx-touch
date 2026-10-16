'use client'

import { useEffect, useRef } from 'react'
import { Check, CircleAlert, CircleCheck, LocateFixed, Radio } from 'lucide-react'
import {
  PROBE_BALL_R,
  PROBE_CLEARANCE,
  PROBE_CYCLES,
  PROBE_FAST_FEED,
  PROBE_FEED,
  PROBE_GROUPS,
  PROBE_STEP_MEASURE,
  PROBE_STEP_RESULT,
  PROBE_STEPS,
  PROBE_TOOL,
  type ProbeCycle,
} from '@/lib/cnc/probe'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatCoord, Keycap, Panel } from '../primitives'
import { ProbeSchematic } from './probe-schematic'

export function ProbeWizard({ state }: { state: MachineState }) {
  const p = state.probe
  const cycle = PROBE_CYCLES[p.cycle]
  const target = state.offsets[state.cursor.offset]

  return (
    <Panel
      title="3D щуп — замер заготовки"
      icon={LocateFixed}
      className="h-full"
      right={
        <span className="flex items-center gap-2 text-xs font-medium text-muted-foreground">
          Запись в
          <span className="rounded-sm bg-primary px-2 py-0.5 font-mono text-sm font-bold text-primary-foreground">
            {target.code}
          </span>
          {p.step > 0 && <Keycap className="h-5 text-[10px]">↑ ↓</Keycap>}
        </span>
      }
    >
      <div className="flex h-full min-h-0 flex-col">
        <Stepper step={p.step} />
        <div className="grid min-h-0 flex-1 grid-cols-[216px_minmax(0,1fr)_300px]">
          <CycleList selected={p.cycle} locked={p.step > 0} />
          <div className="flex min-h-0 flex-col border-x bg-background">
            <div className="flex min-h-0 flex-1">
              <div className="relative min-h-0 min-w-0 flex-1 p-2">
                <div className="h-full overflow-hidden rounded-md border bg-card">
                  <ProbeSchematic
                    cycle={cycle}
                    elapsed={p.elapsed}
                    running={p.running}
                    done={p.step === PROBE_STEP_RESULT}
                  />
                </div>
                <ProbeBadge state={state} />
              </div>
              <Readout state={state} />
            </div>
          </div>
          <StepCard state={state} cycle={cycle} />
        </div>
      </div>
    </Panel>
  )
}

function ProbeBadge({ state }: { state: MachineState }) {
  const ready = state.tool === PROBE_TOOL
  return (
    <div className="absolute right-4 top-4 flex items-center gap-1.5 rounded-sm border bg-card/95 px-2 py-0.5 text-[10px] font-semibold uppercase tracking-wider shadow-sm">
      <Radio className={cn('size-3.5', ready ? 'text-success' : 'text-muted-foreground')} aria-hidden="true" />
      <span className={ready ? 'text-foreground' : 'text-muted-foreground'}>
        {ready ? 'Щуп T99 · готов' : 'Щуп не установлен'}
      </span>
    </div>
  )
}

function Stepper({ step }: { step: number }) {
  return (
    <ol className="flex shrink-0 items-center gap-0 border-b bg-muted/60 px-4 py-1.5" aria-label="Шаги измерения">
      {PROBE_STEPS.map((label, i) => {
        const done = i < step
        const active = i === step
        const last = i === PROBE_STEPS.length - 1
        return (
          <li
            key={label}
            aria-current={active ? 'step' : undefined}
            className={cn('flex items-center gap-2', !last && 'flex-1')}
          >
            <span
              className={cn(
                'flex size-6 shrink-0 items-center justify-center rounded-full border-2 font-mono text-xs font-bold transition-colors',
                done && 'border-success bg-success text-primary-foreground',
                active && 'border-primary bg-primary text-primary-foreground ring-2 ring-primary/25',
                !done && !active && 'border-border bg-card text-muted-foreground',
              )}
            >
              {done ? <Check className="size-3.5" aria-hidden="true" /> : i + 1}
            </span>
            <span
              className={cn(
                'text-[13px] font-semibold',
                active ? 'text-foreground' : done ? 'text-foreground/70' : 'text-muted-foreground',
              )}
            >
              {label}
            </span>
            {!last && (
              <span
                aria-hidden="true"
                className={cn('mx-2 h-0.5 flex-1 rounded-full', done ? 'bg-success' : 'bg-border')}
              />
            )}
          </li>
        )
      })}
    </ol>
  )
}

function CycleList({ selected, locked }: { selected: number; locked: boolean }) {
  const listRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    listRef.current?.querySelector('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest' })
  }, [selected])

  return (
    <div
      ref={listRef}
      className="flex min-h-0 flex-col gap-2 overflow-y-auto bg-card p-2"
      role="listbox"
      aria-label="Циклы измерения"
    >
      {PROBE_GROUPS.map((g) => (
        <div key={g.id} className="flex flex-col gap-1">
          <p className="px-0.5 text-[10px] font-bold uppercase tracking-[0.14em] text-muted-foreground">{g.label}</p>
          <div className={cn(g.id === 'edge' ? 'grid grid-cols-2 gap-1' : 'flex flex-col gap-1')}>
            {PROBE_CYCLES.map((c, i) => ({ c, i }))
              .filter(({ c }) => c.group === g.id)
              .map(({ c, i }) => {
                const sel = i === selected
                const compact = g.id === 'edge'
                return (
                  <div
                    key={c.id}
                    role="option"
                    aria-selected={sel}
                    className={cn(
                      'flex items-center rounded-md border bg-card p-1 transition-colors',
                      compact ? 'gap-1.5' : 'gap-2',
                      sel ? 'border-primary bg-accent ring-2 ring-primary/30' : 'hover:bg-muted/60',
                      locked && !sel && 'opacity-40',
                    )}
                  >
                    <div
                      className={cn('shrink-0 rounded-sm border bg-background p-0.5', compact ? 'h-8 w-9' : 'h-8 w-11')}
                    >
                      <ProbeSchematic cycle={c} mini />
                    </div>
                    <div className="min-w-0">
                      <p className={cn('truncate text-xs leading-tight', sel ? 'font-bold' : 'font-semibold')}>
                        {compact ? c.name.replace('Кромка ', '') : c.name}
                      </p>
                      {!compact && (
                        <p className="truncate font-mono text-[10px] text-muted-foreground">
                          {c.axes.join(' · ')} · {c.touches.length} кас.
                        </p>
                      )}
                    </div>
                  </div>
                )
              })}
          </div>
        </div>
      ))}
    </div>
  )
}

function Readout({ state }: { state: MachineState }) {
  return (
    <dl className="flex w-[124px] shrink-0 flex-col divide-y border-l bg-card" aria-label="Координаты машины">
      {(['X', 'Y', 'Z'] as const).map((ax) => (
        <div key={ax} className="flex flex-1 flex-col justify-center gap-1 px-3">
          <dt className="flex items-center gap-1.5 text-[10px] font-bold uppercase tracking-[0.14em] text-muted-foreground">
            <span className="flex size-5 items-center justify-center rounded-sm bg-foreground font-mono text-xs text-background">
              {ax}
            </span>
            Машина
          </dt>
          <dd className="font-mono text-lg font-bold tabular-nums leading-none">{formatCoord(state.machine[ax])}</dd>
        </div>
      ))}
    </dl>
  )
}

function Spec({ title, children }: { title?: string; children: React.ReactNode }) {
  return (
    <div className="overflow-hidden rounded-md border bg-background">
      {title && (
        <p className="border-b bg-muted px-2.5 py-0.5 text-[10px] font-bold uppercase tracking-[0.14em] text-muted-foreground">
          {title}
        </p>
      )}
      <div className="divide-y px-2.5">{children}</div>
    </div>
  )
}

function Row({ label, value, ok }: { label: string; value?: string; ok?: boolean }) {
  return (
    <div className="flex items-center gap-2 py-1 text-[13px]">
      {ok === true && <CircleCheck className="size-4 shrink-0 text-success" aria-label="В норме" />}
      {ok === false && <CircleAlert className="size-4 shrink-0 text-destructive" aria-label="Не выполнено" />}
      <span
        className={cn(
          'flex-1',
          ok === undefined && 'text-muted-foreground',
          ok === false && 'font-semibold text-destructive',
        )}
      >
        {label}
      </span>
      {value && <span className="font-mono font-semibold tabular-nums">{value}</span>}
    </div>
  )
}

const TITLES = ['Выбор цикла', 'Подготовка щупа', 'Подвод к заготовке', 'Измерение', 'Результат']

function StepCard({ state, cycle }: { state: MachineState; cycle: ProbeCycle }) {
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
