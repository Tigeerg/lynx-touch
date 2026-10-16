'use client'

import { CloudFog, Droplets, Lightbulb, Shovel } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { HwKey, KEY_ROW, LubeIcon, SMALL_KEY } from './hw-parts'

export function AuxBlock({
  state,
  dispatch,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  className?: string
}) {
  return (
    <div role="group" aria-label="Охлаждение и вспомогательные агрегаты" className={cn(KEY_ROW, className)}>
      <div className="flex justify-center gap-2">
        <HwKey
          label="СОЖ"
          icon={Droplets}
          title="Охлаждение (M8)"
          lit={state.coolant}
          tone="primary"
          onClick={() => dispatch({ type: 'TOGGLE', key: 'coolant' })}
          className={SMALL_KEY}
        />
        <HwKey
          label="Туман"
          icon={CloudFog}
          title="Масляный туман / обдув (M7)"
          lit={state.mist}
          tone="primary"
          onClick={() => dispatch({ type: 'TOGGLE', key: 'mist' })}
          className={SMALL_KEY}
        />
        <HwKey
          label="Смазка"
          icon={LubeIcon}
          title="Смазка направляющих: принудительная подача масла"
          lit={state.lube > 0}
          tone="warning"
          onClick={() => dispatch({ type: 'LUBE' })}
          className={SMALL_KEY}
        />
        <HwKey
          label="Свет"
          icon={Lightbulb}
          title="Освещение рабочей зоны"
          lit={state.workLight}
          tone="warning"
          onClick={() => dispatch({ type: 'TOGGLE', key: 'workLight' })}
          className={SMALL_KEY}
        />
        <HwKey
          label="Стружка"
          icon={Shovel}
          title="Конвейер стружки"
          lit={state.chipConveyor}
          tone="success"
          onClick={() => dispatch({ type: 'TOGGLE', key: 'chipConveyor' })}
          className={SMALL_KEY}
        />
      </div>
    </div>
  )
}
