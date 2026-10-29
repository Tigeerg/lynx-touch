'use client'

import {
  CloudFog,
  Droplet,
  Droplets,
  House,
  Lightbulb,
  Lock,
  LockOpen,
  RotateCcw,
  RotateCw,
  Shovel,
  Square,
  Waves,
  Wind,
  Zap,
  type LucideIcon,
} from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Panel } from '../../primitives'
import { KEY_BASE } from './hold-button'

// The spindle, coolant and accessory keys have no hardware equivalent in the touch variant, so they
// get a block of their own to the right of the jog keys: four columns, three rows, every key carrying
// its own state. Lubrication and the safe-point move have no other way in on the touch panel.
export function FunctionsPanel({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
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
