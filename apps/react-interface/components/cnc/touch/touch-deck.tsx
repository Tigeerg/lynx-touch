'use client'

import {
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  ArrowUp,
  ChevronsDown,
  ChevronsUp,
  CloudFog,
  Droplet,
  Droplets,
  House,
  Lightbulb,
  Lock,
  LockOpen,
  Move3d,
  OctagonX,
  Pause,
  Play,
  RotateCcw,
  RotateCw,
  Shovel,
  Square,
  Waves,
  Wind,
  Zap,
  type LucideIcon,
} from 'lucide-react'
import { useEffect, useRef, type ReactNode } from 'react'
import { JOG_STEPS, WORKPLACE_LABEL } from '@/lib/cnc/data'
import type { Action } from '@/lib/cnc/machine'
import { MPG_STEPS, WORKPLACES, type Axis, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { WORKPLACE_ICON } from '../header-bar'
import { Panel } from '../primitives'

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
        // A second pointer on the same key used to orphan the first delay: its interval kept
        // firing after the stop, and only one handle is kept (the review found the leak).
        stop()
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
      tone: 'border-hw-start bg-hw-start text-hw-ink brightness-95',
      idle: 'border-hw-start bg-hw-start text-hw-ink',
      action: { type: 'CYCLE_START' },
    },
    {
      label: 'Стоп',
      icon: Pause,
      lit: state.run === 'HOLD',
      tone: 'border-hw-stop bg-hw-stop text-hw-ink brightness-95',
      idle: 'border-hw-stop bg-hw-stop text-hw-ink',
      action: { type: 'FEED_HOLD' },
    },
    {
      label: 'Сброс',
      icon: RotateCcw,
      lit: state.run === 'ALARM',
      tone: 'border-hw-reset bg-hw-reset text-hw-ink brightness-95',
      idle: 'border-hw-reset bg-hw-reset text-hw-ink',
      action: { type: 'RESET' },
    },
    {
      label: 'Авария',
      icon: OctagonX,
      lit: state.run === 'ESTOP',
      tone: 'border-destructive bg-destructive text-white',
      idle: 'border-destructive bg-destructive/15 text-destructive',
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
            'gap-2.5 border-2 border-b-4 px-3 text-lg uppercase tracking-wide',
            lit ? tone : idle,
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
    <Panel title="Режим" className="w-[150px] shrink-0">
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
                'justify-center gap-1.5 px-1 text-center text-sm whitespace-nowrap',
                active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card active:bg-accent',
              )}
            >
              <Icon className="size-5 shrink-0" aria-hidden="true" />
              <span>{WORKPLACE_LABEL[w]}</span>
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
      <div className="h-full p-1.5">
        <div className={cn('flex h-full min-h-0 gap-1.5', !manual && 'opacity-50')}>
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
      </div>
    </Panel>
  )
}

// The spindle, coolant and accessory keys have no hardware equivalent in the touch variant, so they
// get a block of their own to the right of the jog keys: four columns, three rows, every key carrying
// its own state. Lubrication and the safe-point move have no other way in on the touch panel.
function FunctionsPanel({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const keys: {
    label: string
    icon: LucideIcon
    active: boolean
    action: Action
    title?: string
    span?: number
  }[] = [
    {
      label: 'M3',
      icon: RotateCw,
      active: state.spindleOn && state.spindleDir === 1,
      action: { type: 'SPINDLE_MANUAL', dir: 1 },
    },
    {
      label: 'M4',
      icon: RotateCcw,
      active: state.spindleOn && state.spindleDir === -1,
      action: { type: 'SPINDLE_MANUAL', dir: -1 },
    },
    { label: 'M5', icon: Square, active: !state.spindleOn, action: { type: 'SPINDLE_STOP' } },
    { label: 'СОЖ', icon: Droplets, active: state.coolant, action: { type: 'TOGGLE', key: 'coolant' } },
    { label: 'Туман', icon: CloudFog, active: state.mist, action: { type: 'TOGGLE', key: 'mist' } },
    { label: 'Свет', icon: Lightbulb, active: state.workLight, action: { type: 'TOGGLE', key: 'workLight' } },
    { label: 'Стружка', icon: Shovel, active: state.chipConveyor, action: { type: 'TOGGLE', key: 'chipConveyor' } },
    { label: 'Обдув', icon: Wind, active: state.airBlast, action: { type: 'TOGGLE', key: 'airBlast' } },
    { label: 'Промывка', icon: Waves, active: state.wash, action: { type: 'TOGGLE', key: 'wash' } },
    {
      label: 'Смазка',
      title: 'Смазка направляющих',
      icon: Droplet,
      active: state.lube > 0,
      action: { type: 'LUBE' },
    },
    {
      label: 'Отвод',
      title: 'Безопасный отвод в безопасную точку',
      icon: House,
      active: false,
      action: { type: 'GO_SAFE' },
      span: 3,
    },
    {
      label: state.toolClamped ? 'Разжим' : 'Зажим',
      title: state.toolClamped ? 'Разжать инструмент' : 'Зажать инструмент',
      icon: state.toolClamped ? LockOpen : Lock,
      active: state.toolClamped,
      action: { type: 'TOOL_CLAMP', clamp: !state.toolClamped },
    },
  ]

  return (
    <Panel title="Функции" icon={Zap} className="w-[300px] shrink-0">
      <div className="grid h-full grid-cols-4 grid-rows-3 gap-1.5 p-1.5" role="group" aria-label="Функции станка">
        {keys.map(({ label, icon: Icon, active, action, title, span }) => (
          <button
            key={label}
            type="button"
            tabIndex={-1}
            aria-pressed={active}
            aria-label={title ?? label}
            title={title}
            onClick={() => dispatch(action)}
            style={span ? { gridColumn: `span ${span} / span ${span}` } : undefined}
            className={cn(
              KEY_BASE,
              'flex-col gap-0.5 px-1 text-[11px]',
              active ? 'border-primary bg-primary text-primary-foreground' : 'bg-card active:bg-accent',
            )}
          >
            <Icon className="size-5" aria-hidden="true" />
            {label}
          </button>
        ))}
      </div>
    </Panel>
  )
}

export function TouchDeck({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  return (
    <section aria-label="Сенсорный пульт" className="flex shrink-0 flex-col gap-1.5">
      <div className="flex h-[280px] gap-1.5">
        <WorkplaceColumn state={state} dispatch={dispatch} />
        <JogPanel state={state} dispatch={dispatch} />
        <FunctionsPanel state={state} dispatch={dispatch} />
      </div>
      <CycleRow state={state} dispatch={dispatch} />
    </section>
  )
}
