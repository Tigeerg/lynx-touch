'use client'

import {
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  ArrowUp,
  ChevronsDown,
  ChevronsUp,
  Rabbit,
  RotateCcw,
  RotateCw,
  type LucideIcon,
} from 'lucide-react'
import { RAPID_JOG_MULT } from '@/lib/cnc/data'
import { type Axis, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { HwKey } from '../hw-parts'

export function JogPad({
  state,
  onJog,
  onToggleRapid,
}: {
  state: MachineState
  onJog: (axis: Axis, dir: 1 | -1) => void
  onToggleRapid: () => void
}) {
  const key = (axis: Axis, dir: 1 | -1, icon: LucideIcon, area: string) => (
    <HwKey
      label={`${axis}${dir > 0 ? '+' : '−'}`}
      icon={icon}
      title={`Ось ${axis} ${dir > 0 ? 'плюс' : 'минус'}`}
      lit={state.jogAxis === axis}
      tone="primary"
      onClick={() => onJog(axis, dir)}
      className={cn('gap-0 p-1 text-sm font-bold', area)}
    />
  )
  return (
    <div
      role="group"
      aria-label="Направления перемещения"
      className="grid shrink-0 grid-cols-[repeat(3,4.5rem)] grid-rows-[repeat(3,4.5rem)] gap-x-2 gap-y-4 pt-4"
    >
      {key('Z', 1, ChevronsUp, 'col-start-1 row-start-1')}
      {key('Y', 1, ArrowUp, 'col-start-2 row-start-1')}
      {key('A', 1, RotateCw, 'col-start-3 row-start-1')}
      {key('X', -1, ArrowLeft, 'col-start-1 row-start-2')}
      <HwKey
        label="Уск."
        icon={Rabbit}
        title={`Ускоренное перемещение: шаг ×${RAPID_JOG_MULT}`}
        lit={state.jogRapid}
        tone="warning"
        cap="bg-hw-amber text-hw-ink"
        onClick={onToggleRapid}
        className="col-start-2 row-start-2 gap-0 p-1 text-[12px] font-extrabold uppercase [&_svg]:size-7"
      />
      {key('X', 1, ArrowRight, 'col-start-3 row-start-2')}
      {key('Z', -1, ChevronsDown, 'col-start-1 row-start-3')}
      {key('Y', -1, ArrowDown, 'col-start-2 row-start-3')}
      {key('A', -1, RotateCcw, 'col-start-3 row-start-3')}
    </div>
  )
}
