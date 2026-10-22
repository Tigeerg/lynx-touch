'use client'

import { Play, Ruler, X } from 'lucide-react'
import { useId, type Dispatch } from 'react'
import type { Action } from '@/lib/cnc/machine'
import {
  MEASURE_PARAMS,
  MEASURE_TOTAL_S,
  measureFrame,
  type MeasureFrame,
  type MeasurePhase,
} from '@/lib/cnc/tool-measure'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Overlay } from './overlay'

const PHASE_LABEL: Record<MeasurePhase, string> = {
  idle: 'Готов к замеру',
  rapid: 'Быстрый подвод',
  fine: 'Медленный подвод',
  touch: 'Касание датчика',
  retract: 'Отвод шпинделя',
  done: 'Замер завершён',
}

const TRAVEL = 92

export function ToolMeasureDialog({ state, dispatch }: { state: MachineState; dispatch: Dispatch<Action> }) {
  const titleId = useId()
  const m = state.toolMeasure
  const tool = state.tools[state.cursor.tool]
  const frame = measureFrame(m)
  const close = () => dispatch({ type: 'TOOL_MEASURE_CLOSE' })
  const zNow = state.machine.Z - frame.depth * 80
  const delta = m.length !== null ? m.length - tool.length : null

  return (
    <Overlay onClose={close} labelledBy={titleId} className="w-full max-w-4xl border-2 border-foreground/20">
      <header className="flex items-center gap-2 border-b bg-muted px-4 py-2">
        <Ruler className="size-4 text-primary" aria-hidden="true" />
        <h2 id={titleId} className="text-xs font-bold uppercase tracking-wide">
          Замер длины инструмента · датчик TOOL SETTER
        </h2>
        <button
          type="button"
          tabIndex={-1}
          aria-label="Закрыть"
          className="ml-auto rounded p-1 hover:bg-accent"
          onClick={close}
        >
          <X className="size-4" aria-hidden="true" />
        </button>
      </header>

      <div className="grid grid-cols-[13rem_minmax(0,1fr)_14rem] gap-px bg-border">
        <section aria-label="Параметры датчика" className="flex flex-col gap-1 bg-card p-3">
          <h3 className="mb-1 text-[11px] font-bold uppercase tracking-wider text-muted-foreground">Параметры</h3>
          <Param label="Быстрый подвод" value={MEASURE_PARAMS.rapidFeed.toFixed(1)} unit="мм/мин" />
          <Param label="Медл. подвод" value={MEASURE_PARAMS.fineFeed.toFixed(1)} unit="мм/мин" />
          <Param label="Ход по Z" value={MEASURE_PARAMS.zTravel.toFixed(1)} unit="мм" />
          <Param label="Отвод" value={MEASURE_PARAMS.retract.toFixed(1)} unit="мм" />
          <Param label="Допуск поломки" value={MEASURE_PARAMS.breakageTolerance.toFixed(1)} unit="мм" />
          <div className="mt-2 border-t pt-2">
            <Param label="Инструмент" value={`T${String(tool.number).padStart(2, '0')}`} />
            <Param label="Диаметр" value={tool.diameter.toFixed(3)} unit="мм" />
          </div>
        </section>

        <section
          aria-label="Схема замера"
          className="relative flex min-h-[22rem] items-center justify-center bg-background"
        >
          <MeasureScene frame={frame} length={m.length} />
        </section>

        <section aria-label="Ход замера" className="flex flex-col gap-2 bg-card p-3">
          <h3 className="text-[11px] font-bold uppercase tracking-wider text-muted-foreground">Состояние</h3>
          <p
            aria-live="polite"
            className={cn(
              'border px-2 py-1.5 text-center font-mono text-xs font-bold uppercase',
              frame.triggered && 'border-warning bg-warning text-warning-foreground',
              frame.phase === 'done' && 'border-success bg-success text-primary-foreground',
            )}
          >
            {PHASE_LABEL[frame.phase]}
          </p>

          <div
            role="progressbar"
            aria-label="Ход замера"
            aria-valuemin={0}
            aria-valuemax={100}
            aria-valuenow={Math.round(frame.progress * 100)}
            className="h-3 overflow-hidden border bg-muted"
          >
            <div
              className="h-full bg-primary transition-[width] duration-75"
              style={{ width: `${frame.progress * 100}%` }}
            />
          </div>
          <p className="text-right font-mono text-[11px] tabular-nums text-muted-foreground">
            {Math.min(m.elapsed, MEASURE_TOTAL_S).toFixed(1)} / {MEASURE_TOTAL_S.toFixed(1)} с
          </p>

          <dl className="flex flex-col gap-1 font-mono tabular-nums">
            <Readout label="Z шпинделя" value={zNow.toFixed(3)} />
            <Readout
              label="Длина"
              value={m.length === null ? '—' : m.length.toFixed(3)}
              tone={m.length === null ? undefined : 'text-success'}
            />
            <Readout label="Было" value={tool.length.toFixed(3)} />
            <Readout
              label="Δ"
              value={delta === null ? '—' : `${delta >= 0 ? '+' : ''}${delta.toFixed(3)}`}
              tone={
                delta === null
                  ? undefined
                  : Math.abs(delta) > MEASURE_PARAMS.breakageTolerance
                    ? 'text-destructive'
                    : 'text-success'
              }
            />
          </dl>
        </section>
      </div>

      <footer className="flex items-center gap-2 border-t bg-muted px-4 py-2">
        <p className="mr-auto text-xs text-muted-foreground">
          {m.running
            ? 'Идёт замер — шпиндель движется к датчику'
            : m.length === null
              ? 'Убедитесь, что над датчиком свободно. Enter или ПУСК — начать замер'
              : `Длина записана в таблицу: T${String(tool.number).padStart(2, '0')}`}
        </p>
        <button
          type="button"
          disabled={m.running}
          onClick={() => dispatch({ type: 'TOOL_MEASURE_START' })}
          className="flex h-10 items-center gap-2 border-2 border-primary bg-primary px-4 text-sm font-bold uppercase text-primary-foreground hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-40"
        >
          <Play className="size-4" aria-hidden="true" />
          {m.length === null ? 'Пуск замера' : 'Повторить'}
        </button>
        <button
          type="button"
          onClick={close}
          className="flex h-10 items-center gap-2 border-2 bg-card px-4 text-sm font-bold uppercase hover:bg-accent"
        >
          {m.running ? 'Прервать' : 'Закрыть'}
        </button>
      </footer>
    </Overlay>
  )
}

function Param({ label, value, unit }: { label: string; value: string; unit?: string }) {
  return (
    <div className="flex flex-col gap-0.5">
      <span className="text-[11px] text-muted-foreground">{label}</span>
      <span className="flex items-baseline justify-between border bg-muted px-2 py-1 font-mono text-sm font-bold tabular-nums">
        {value}
        {unit && <span className="text-[10px] font-normal text-muted-foreground">{unit}</span>}
      </span>
    </div>
  )
}

function Readout({ label, value, tone }: { label: string; value: string; tone?: string }) {
  return (
    <div className="flex items-baseline justify-between border bg-muted px-2 py-1">
      <dt className="font-sans text-[11px] text-muted-foreground">{label}</dt>
      <dd className={cn('text-sm font-bold', tone)}>{value}</dd>
    </div>
  )
}

function MeasureScene({ frame, length }: { frame: MeasureFrame; length: number | null }) {
  const uid = useId().replace(/:/g, '')
  const dy = frame.depth * TRAVEL
  const moving = frame.phase === 'rapid' || frame.phase === 'fine' || frame.phase === 'retract'
  const down = frame.phase === 'rapid' || frame.phase === 'fine'
  const plunge = frame.triggered ? 3 : 0
  const tipRest = 172
  const contactY = tipRest + TRAVEL

  return (
    <svg
      viewBox="0 0 360 340"
      className="block h-full max-h-[22rem] w-full"
      role="img"
      aria-label="Шпиндель с инструментом и измерительный датчик"
    >
      <defs>
        <linearGradient id={`metal-${uid}`} x1="0" x2="1" y1="0" y2="0">
          <stop offset="0" style={{ stopColor: 'var(--muted-foreground)', stopOpacity: 0.55 }} />
          <stop offset="0.35" style={{ stopColor: 'var(--card)', stopOpacity: 1 }} />
          <stop offset="0.6" style={{ stopColor: 'var(--muted-foreground)', stopOpacity: 0.8 }} />
          <stop offset="1" style={{ stopColor: 'var(--muted-foreground)', stopOpacity: 0.4 }} />
        </linearGradient>
        <linearGradient id={`gold-${uid}`} x1="0" x2="1" y1="0" y2="0">
          <stop offset="0" style={{ stopColor: 'var(--warning)', stopOpacity: 0.7 }} />
          <stop offset="0.45" style={{ stopColor: 'var(--warning)', stopOpacity: 1 }} />
          <stop offset="1" style={{ stopColor: 'var(--warning)', stopOpacity: 0.55 }} />
        </linearGradient>
        <linearGradient id={`dark-${uid}`} x1="0" x2="1" y1="0" y2="0">
          <stop offset="0" style={{ stopColor: 'var(--foreground)', stopOpacity: 0.55 }} />
          <stop offset="0.5" style={{ stopColor: 'var(--foreground)', stopOpacity: 0.85 }} />
          <stop offset="1" style={{ stopColor: 'var(--foreground)', stopOpacity: 0.5 }} />
        </linearGradient>
        <marker
          id={`arr-${uid}`}
          viewBox="0 0 10 10"
          refX="8"
          refY="5"
          markerWidth="6"
          markerHeight="6"
          orient="auto-start-reverse"
        >
          <path d="M0,0 L10,5 L0,10 z" className="fill-foreground" />
        </marker>
      </defs>

      <g className="stroke-border" strokeWidth="0.5">
        {Array.from({ length: 17 }, (_, i) => (
          <line key={`gx${i}`} x1={i * 22.5} y1="0" x2={i * 22.5} y2="340" />
        ))}
        {Array.from({ length: 16 }, (_, i) => (
          <line key={`gy${i}`} x1="0" y1={i * 22.5} x2="360" y2={i * 22.5} />
        ))}
      </g>

      <line
        x1="180"
        y1="6"
        x2="180"
        y2="334"
        className="stroke-foreground/40"
        strokeWidth="1"
        strokeDasharray="8 4 2 4"
      />

      <g>
        <line
          x1="40"
          y1={contactY}
          x2="132"
          y2={contactY}
          className="stroke-foreground/60"
          strokeWidth="1"
          strokeDasharray="4 3"
        />
        <text x="40" y={contactY - 6} className="fill-muted-foreground font-sans text-[10px]">
          Точка срабатывания
        </text>
      </g>

      <g transform={`translate(0 ${dy})`} style={{ transition: 'transform 60ms linear' }}>
        <rect
          x="132"
          y="6"
          width="96"
          height="46"
          fill={`url(#metal-${uid})`}
          className="stroke-foreground/70"
          strokeWidth="1.5"
        />
        <rect x="132" y="6" width="96" height="9" className="fill-foreground/30" />
        <line x1="140" y1="34" x2="220" y2="34" className="stroke-foreground/30" strokeWidth="1" />
        <path
          d="M148,52 L212,52 L202,84 L158,84 Z"
          fill={`url(#metal-${uid})`}
          className="stroke-foreground/70"
          strokeWidth="1.5"
        />
        <rect
          x="162"
          y="84"
          width="36"
          height="22"
          fill={`url(#dark-${uid})`}
          className="stroke-foreground/80"
          strokeWidth="1.5"
        />
        <g className="stroke-background/50" strokeWidth="1.5">
          <line x1="170" y1="86" x2="170" y2="104" />
          <line x1="180" y1="86" x2="180" y2="104" />
          <line x1="190" y1="86" x2="190" y2="104" />
        </g>
        <rect
          x="173"
          y="106"
          width="14"
          height="26"
          fill={`url(#metal-${uid})`}
          className="stroke-foreground/70"
          strokeWidth="1.2"
        />
        <path
          d={`M167,132 L193,132 L193,168 L180,${tipRest} L167,168 Z`}
          fill={`url(#gold-${uid})`}
          className="stroke-foreground/80"
          strokeWidth="1.5"
        />
        <g className="stroke-warning-foreground/60" strokeWidth="1.5">
          <line x1="167" y1="140" x2="193" y2="150" />
          <line x1="167" y1="150" x2="193" y2="160" />
          <line x1="167" y1="160" x2="193" y2="170" />
        </g>

        <g className="stroke-foreground" strokeWidth="1">
          <line x1="232" y1="106" x2="262" y2="106" strokeDasharray="3 2" />
          <line x1="232" y1={tipRest} x2="262" y2={tipRest} strokeDasharray="3 2" />
          <line
            x1="256"
            y1="108"
            x2="256"
            y2={tipRest - 2}
            markerStart={`url(#arr-${uid})`}
            markerEnd={`url(#arr-${uid})`}
          />
        </g>
        <text x="262" y="142" className="fill-foreground font-sans text-[11px] font-semibold">
          Длина
        </text>
        <text x="262" y="156" className="fill-foreground font-sans text-[11px] font-semibold">
          инструмента
        </text>
        {length !== null && (
          <text x="262" y="172" className="fill-success font-mono text-[12px] font-bold">
            {length.toFixed(3)}
          </text>
        )}
      </g>

      {moving && (
        <g className={down ? 'fill-primary' : 'fill-success'} transform={`translate(104 ${100 + dy * 0.6})`}>
          {down ? (
            <path d="M0,0 L14,0 L14,16 L22,16 L7,32 L-8,16 L0,16 Z" />
          ) : (
            <path d="M7,0 L22,16 L14,16 L14,32 L0,32 L0,16 L-8,16 Z" />
          )}
        </g>
      )}

      <g>
        <rect
          x="140"
          y="312"
          width="80"
          height="14"
          className="fill-foreground/70 stroke-foreground"
          strokeWidth="1.5"
        />
        <path
          d="M150,312 L150,296 L210,296 L210,312 Z"
          fill={`url(#dark-${uid})`}
          className="stroke-foreground"
          strokeWidth="1.5"
        />
        <rect
          x="168"
          y="278"
          width="24"
          height="18"
          fill={`url(#metal-${uid})`}
          className="stroke-foreground"
          strokeWidth="1.5"
        />
        <rect
          x="160"
          y={contactY + plunge}
          width="40"
          height="8"
          className={cn('stroke-foreground', frame.triggered ? 'fill-warning' : 'fill-muted-foreground')}
          strokeWidth="1.5"
        />
        <rect
          x="224"
          y="300"
          width="22"
          height="12"
          className="fill-foreground/60 stroke-foreground"
          strokeWidth="1.2"
        />
        <circle cx="235" cy="306" r="3" className={frame.triggered ? 'fill-warning' : 'fill-foreground/30'} />
      </g>

      {frame.triggered && (
        <g>
          <circle
            cx="180"
            cy={contactY + 4}
            r="22"
            className="fill-none stroke-warning"
            strokeWidth="2.5"
            strokeDasharray="5 4"
          />
          <circle cx="180" cy={contactY + 4} r="34" className="fill-none stroke-warning/50" strokeWidth="1.5" />
          <text
            x="180"
            y={contactY + 52}
            textAnchor="middle"
            className="fill-warning font-mono text-[12px] font-bold uppercase"
          >
            Датчик сработал
          </text>
        </g>
      )}

      <text
        x="180"
        y="334"
        textAnchor="middle"
        className="fill-muted-foreground font-sans text-[10px] uppercase tracking-wider"
      >
        Измерительный датчик
      </text>
    </svg>
  )
}
