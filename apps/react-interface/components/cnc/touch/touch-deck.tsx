'use client'

import {
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  ArrowUp,
  ChevronsDown,
  ChevronsUp,
  Gauge,
  Minus,
  Move3d,
  OctagonX,
  Pause,
  Play,
  Plus,
  RotateCcw,
  RotateCw,
  Zap,
  type LucideIcon,
} from 'lucide-react'
import { useEffect, useRef, type ReactNode } from 'react'
import { JOG_STEPS, RAPID_STEPS, WORKPLACE_LABEL } from '@/lib/cnc/data'
import type { Action } from '@/lib/cnc/machine'
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM } from '@/lib/cnc/service-data'
import { MPG_STEPS, WORKPLACES, type Axis, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { WORKPLACE_ICON } from '../header-bar'
import { OverrideBar, Panel } from '../primitives'

const HOLD_DELAY_MS = 400
const HOLD_REPEAT_MS = 90

const KEY_BASE =
  'flex select-none items-center justify-center rounded-md border border-b-[3px] font-semibold transition-[transform,background-color] active:translate-y-px active:border-b'

function HoldButton({
  onFire,
  className,
  label,
  children,
}: {
  onFire: () => void
  className?: string
  label: string
  children: ReactNode
}) {
  const timers = useRef<{ delay?: number; repeat?: number }>({})
  const stop = () => {
    window.clearTimeout(timers.current.delay)
    window.clearInterval(timers.current.repeat)
    timers.current = {}
  }
  useEffect(() => stop, [])

  return (
    <button
      type="button"
      tabIndex={-1}
      aria-label={label}
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId)
        onFire()
        timers.current.delay = window.setTimeout(() => {
          timers.current.repeat = window.setInterval(onFire, HOLD_REPEAT_MS)
        }, HOLD_DELAY_MS)
      }}
      onPointerUp={stop}
      onPointerCancel={stop}
      onLostPointerCapture={stop}
      onContextMenu={(e) => e.preventDefault()}
      className={cn(KEY_BASE, className)}
    >
      {children}
    </button>
  )
}

type CycleItem = { label: string; icon: LucideIcon; lit: boolean; tone: string; idle: string; action: Action }

function CycleRow({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const items: CycleItem[] = [
    {
      label: 'Пуск',
      icon: Play,
      lit: state.run === 'RUN',
      tone: 'border-success bg-success text-white',
      idle: 'border-success/60 text-success',
      action: { type: 'CYCLE_START' },
    },
    {
      label: 'Стоп',
      icon: Pause,
      lit: state.run === 'HOLD',
      tone: 'border-warning bg-warning text-warning-foreground',
      idle: 'border-warning/70 text-warning-foreground',
      action: { type: 'FEED_HOLD' },
    },
    {
      label: 'Сброс',
      icon: RotateCcw,
      lit: state.run === 'ALARM',
      tone: 'border-primary bg-primary text-primary-foreground',
      idle: 'border-primary/60 text-primary',
      action: { type: 'RESET' },
    },
    {
      label: 'Авария',
      icon: OctagonX,
      lit: state.run === 'ESTOP',
      tone: 'border-destructive bg-destructive text-white',
      idle: 'border-destructive/60 text-destructive',
      action: { type: 'ESTOP' },
    },
  ]
  return (
    <div className="grid h-[72px] shrink-0 grid-cols-4 gap-1.5" role="group" aria-label="Управление циклом">
      {items.map(({ label, icon: Icon, lit, tone, idle, action }) => (
        <button
          key={label}
          type="button"
          tabIndex={-1}
          aria-pressed={lit}
          onClick={() => dispatch(action)}
          className={cn(
            KEY_BASE,
            'gap-2.5 border-2 border-b-4 text-lg uppercase tracking-wide',
            lit ? tone : cn('bg-card', idle),
          )}
        >
          <Icon className="size-7" aria-hidden="true" />
          {label}
        </button>
      ))}
    </div>
  )
}

function WorkplaceColumn({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  return (
    <Panel title="Режим" className="w-[132px] shrink-0">
      <div className="grid h-full grid-rows-3 gap-1.5 p-1.5" role="radiogroup" aria-label="Режим">
        {WORKPLACES.map((w) => {
          const Icon = WORKPLACE_ICON[w]
          const active = w === state.workplace
          return (
            <button
              key={w}
              type="button"
              role="radio"
              aria-checked={active}
              tabIndex={-1}
              onClick={() => dispatch({ type: 'SET_WORKPLACE', workplace: w })}
              className={cn(
                KEY_BASE,
                'justify-start gap-2 px-2 text-sm',
                active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card active:bg-accent',
              )}
            >
              <Icon className="size-5 shrink-0" aria-hidden="true" />
              <span className="truncate">{WORKPLACE_LABEL[w]}</span>
            </button>
          )
        })}
      </div>
    </Panel>
  )
}

type JogKey = { axis: Axis; dir: 1 | -1; icon: LucideIcon } | 'rapid'

const JOG_GRID: JogKey[] = [
  { axis: 'Z', dir: 1, icon: ChevronsUp },
  { axis: 'Y', dir: 1, icon: ArrowUp },
  { axis: 'A', dir: 1, icon: RotateCw },
  { axis: 'X', dir: -1, icon: ArrowLeft },
  'rapid',
  { axis: 'X', dir: 1, icon: ArrowRight },
  { axis: 'Z', dir: -1, icon: ChevronsDown },
  { axis: 'Y', dir: -1, icon: ArrowDown },
  { axis: 'A', dir: -1, icon: RotateCcw },
]

function JogPanel({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const manual = state.mode === 'JOG' || state.mode === 'MPG'
  const mpg = state.mode === 'MPG'
  const steps = mpg ? MPG_STEPS : JOG_STEPS

  return (
    <Panel
      title="Перемещение"
      icon={Move3d}
      className="min-w-0 flex-1"
      right={
        <span className={cn('font-mono text-xs font-semibold', manual ? 'text-foreground' : 'text-muted-foreground')}>
          {manual ? `шаг ${state.jogStep} мм` : 'только РУЧНОЙ / МАХОВИК'}
        </span>
      }
    >
      <div className={cn('flex h-full gap-1.5 p-1.5', !manual && 'opacity-50')}>
        <div className="grid flex-1 grid-cols-3 grid-rows-3 gap-1.5">
          {JOG_GRID.map((k) => {
            if (k === 'rapid') {
              return (
                <button
                  key="rapid"
                  type="button"
                  tabIndex={-1}
                  aria-pressed={state.jogRapid}
                  onClick={() => dispatch({ type: 'TOGGLE', key: 'jogRapid' })}
                  className={cn(
                    KEY_BASE,
                    'flex-col gap-0.5 text-xs',
                    state.jogRapid ? 'border-warning bg-warning text-warning-foreground' : 'bg-card active:bg-accent',
                  )}
                >
                  <Zap className="size-5" aria-hidden="true" />
                  Ускор.
                </button>
              )
            }
            const Icon = k.icon
            const selected = manual && state.jogAxis === k.axis
            return (
              <HoldButton
                key={`${k.axis}${k.dir}`}
                label={`${k.axis}${k.dir > 0 ? '+' : '−'}`}
                onFire={() => dispatch({ type: 'JOG', axis: k.axis, dir: k.dir })}
                className={cn(
                  'gap-1 bg-card font-mono text-lg font-bold active:bg-accent',
                  selected && 'border-primary/60 text-primary',
                )}
              >
                <Icon className="size-5" aria-hidden="true" />
                {k.axis}
                {k.dir > 0 ? '+' : '−'}
              </HoldButton>
            )
          })}
        </div>
        <div className="grid w-[68px] shrink-0 grid-rows-3 gap-1.5" role="radiogroup" aria-label="Шаг перемещения">
          {steps.map((step, i) => {
            const active = manual && state.jogStep === step
            return (
              <button
                key={step}
                type="button"
                role="radio"
                aria-checked={active}
                tabIndex={-1}
                onClick={() => dispatch({ type: 'SET_STEP', step })}
                className={cn(
                  KEY_BASE,
                  'font-mono text-sm font-bold',
                  active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card active:bg-accent',
                )}
              >
                {mpg ? `×${10 ** i}` : step}
              </button>
            )
          })}
        </div>
      </div>
    </Panel>
  )
}

function OverrideControl({
  label,
  value,
  max,
  onStep,
}: {
  label: string
  value: number
  max: number
  onStep: (dir: 1 | -1) => void
}) {
  return (
    <div className="flex min-h-0 items-center gap-1.5">
      <HoldButton
        label={`${label} меньше`}
        onFire={() => onStep(-1)}
        className="h-full w-12 shrink-0 bg-card active:bg-accent"
      >
        <Minus className="size-5" aria-hidden="true" />
      </HoldButton>
      <div className="flex min-w-0 flex-1 flex-col gap-1">
        <div className="flex items-baseline justify-between gap-1.5">
          <span className="min-w-0 truncate text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
            {label}
          </span>
          <span className="shrink-0 font-mono text-base font-bold tabular-nums">{value}%</span>
        </div>
        <OverrideBar value={value} max={max} />
      </div>
      <HoldButton
        label={`${label} больше`}
        onFire={() => onStep(1)}
        className="h-full w-12 shrink-0 bg-card active:bg-accent"
      >
        <Plus className="size-5" aria-hidden="true" />
      </HoldButton>
    </div>
  )
}

function OverridePanel({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  return (
    <Panel title="Корректоры" icon={Gauge} className="w-[236px] shrink-0">
      <div className="grid h-full grid-rows-3 gap-2 p-1.5">
        <OverrideControl
          label="Подача"
          value={state.feedOverride}
          max={state.params[MAX_FEED_PARAM] ?? 150}
          onStep={(dir) => dispatch({ type: 'OVERRIDE', kind: 'feed', dir })}
        />
        <OverrideControl
          label="Шпиндель"
          value={state.spindleOverride}
          max={state.params[MAX_SPINDLE_PARAM] ?? 120}
          onStep={(dir) => dispatch({ type: 'OVERRIDE', kind: 'spindle', dir })}
        />
        <OverrideControl
          label="Ускор. G0"
          value={state.rapidOverride}
          max={RAPID_STEPS[RAPID_STEPS.length - 1]}
          onStep={(dir) => dispatch({ type: 'OVERRIDE', kind: 'rapid', dir })}
        />
      </div>
    </Panel>
  )
}

export function TouchDeck({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  return (
    <section aria-label="Сенсорный пульт" className="flex shrink-0 flex-col gap-1.5">
      <div className="flex h-[212px] gap-1.5">
        <WorkplaceColumn state={state} dispatch={dispatch} />
        <JogPanel state={state} dispatch={dispatch} />
        <OverridePanel state={state} dispatch={dispatch} />
      </div>
      <CycleRow state={state} dispatch={dispatch} />
    </section>
  )
}
