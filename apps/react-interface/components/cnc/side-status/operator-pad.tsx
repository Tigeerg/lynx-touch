import { OctagonX, Pause, Play, RotateCcw, type LucideIcon } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap, Panel } from '../primitives'

type PadItem = {
  label: string
  short: string
  keyLabel: string
  icon: LucideIcon
  lit: boolean
  /** The face of the key while the state is off: a pale tint of the key's own colour. */
  cap: string
  /** The face of the key while the state is on: the same colour, saturated. */
  tone: string
  action: Action
}

export function OperatorPad({
  state,
  dispatch,
  layout = 'grid',
}: {
  state: MachineState
  dispatch: (a: Action) => void
  layout?: 'grid' | 'row' | 'compact'
}) {
  const items: PadItem[] = [
    {
      label: 'Пуск цикла',
      short: 'Пуск',
      keyLabel: 'Enter',
      icon: Play,
      lit: state.run === 'RUN',
      cap: 'border-hw-start bg-hw-start text-hw-ink',
      tone: 'border-success bg-success text-white',
      action: { type: 'CYCLE_START' },
    },
    {
      label: 'Стоп подачи',
      short: 'Стоп',
      keyLabel: 'Space',
      icon: Pause,
      lit: state.run === 'HOLD',
      cap: 'border-hw-stop bg-hw-stop text-hw-ink',
      tone: 'border-warning bg-warning text-warning-foreground',
      action: { type: 'FEED_HOLD' },
    },
    {
      label: 'Сброс',
      short: 'Сброс',
      keyLabel: 'Esc',
      icon: RotateCcw,
      lit: state.run === 'ALARM',
      cap: 'border-hw-reset bg-hw-reset text-hw-ink',
      tone: 'border-primary bg-primary text-primary-foreground',
      action: { type: 'RESET' },
    },
    {
      label: 'Авар. стоп',
      short: 'Авария',
      keyLabel: 'Shift+Esc',
      icon: OctagonX,
      lit: state.run === 'ESTOP',
      cap: 'border-destructive bg-destructive/15 text-destructive',
      tone: 'border-destructive bg-destructive text-white',
      action: { type: 'ESTOP' },
    },
  ]

  const compact = layout === 'compact'
  const buttons = items.map((item) => (
    <OperatorButton key={item.keyLabel} item={item} compact={compact} onClick={() => dispatch(item.action)} />
  ))

  if (compact) {
    // One gutter for the whole pad: the same 6 px between the keys and to the edges of the block,
    // as in the framed layout below.
    return (
      <div className="grid shrink-0 grid-cols-4 gap-1.5 p-1.5" role="group" aria-label="Пульт оператора">
        {buttons}
      </div>
    )
  }

  return (
    <Panel title="Пульт оператора" className="shrink-0">
      <div className={cn('grid gap-1.5 p-1.5', layout === 'row' ? 'grid-cols-4' : 'grid-cols-2')}>{buttons}</div>
    </Panel>
  )
}

function OperatorButton({ item, compact, onClick }: { item: PadItem; compact: boolean; onClick: () => void }) {
  const Icon = item.icon
  return (
    <button
      type="button"
      tabIndex={-1}
      onClick={onClick}
      aria-pressed={item.lit}
      title={`${item.label} (${item.keyLabel})`}
      className={cn(
        'rounded-md border-2 transition-colors',
        compact
          ? 'flex h-10 flex-col items-center justify-center gap-0.5 px-2'
          : 'flex h-11 items-center gap-2 px-2.5 text-left',
        item.lit ? item.tone : item.cap,
      )}
    >
      <Icon className={cn('shrink-0', compact ? 'size-4' : 'size-5')} aria-hidden="true" />
      {compact ? (
        <span className="text-[10px] font-bold leading-none">{item.short}</span>
      ) : (
        <>
          <span className="truncate text-xs font-bold leading-none">{item.label}</span>
          <Keycap className="ml-auto h-5 text-[10px] text-foreground/80">{item.keyLabel}</Keycap>
        </>
      )}
    </button>
  )
}
