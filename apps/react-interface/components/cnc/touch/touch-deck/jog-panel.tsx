'use client'

import {
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  ArrowUp,
  ChevronsDown,
  ChevronsUp,
  Move3d,
  RotateCcw,
  RotateCw,
  Zap,
  type LucideIcon,
} from 'lucide-react'
import { JOG_STEPS } from '@/lib/cnc/data'
import type { Action } from '@/lib/cnc/machine'
import { MPG_STEPS, type Axis, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Panel } from '../../primitives'
import { HoldButton, KEY_BASE } from './hold-button'

type JogKey = { axis: Axis; dir: 1 | -1; icon: LucideIcon } | 'rapid'

export const JOG_GRID: JogKey[] = [
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

export function JogPanel({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
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
