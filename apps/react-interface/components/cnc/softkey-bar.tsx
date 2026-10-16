import type { Softkey } from '@/lib/cnc/softkeys'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Info, CircleAlert, TriangleAlert } from 'lucide-react'
import { Keycap } from './primitives'

export function MessageBar({ message }: { message: MachineState['message'] }) {
  const Icon = message?.level === 'error' ? CircleAlert : message?.level === 'warn' ? TriangleAlert : Info
  return (
    <div
      role="status"
      aria-live="polite"
      className="pointer-events-none absolute inset-x-0 bottom-16 z-20 flex justify-center px-3"
    >
      {message && (
        <div
          className={cn(
            'flex max-w-full items-center gap-2 rounded-md border px-3 py-1.5 text-xs font-semibold shadow-lg',
            message.level === 'info' && 'border-primary/40 bg-card text-foreground',
            message.level === 'warn' && 'border-warning bg-warning text-warning-foreground',
            message.level === 'error' && 'border-2 border-destructive bg-card text-destructive',
          )}
        >
          <Icon className={cn('size-4 shrink-0', message.level === 'info' && 'text-primary')} aria-hidden="true" />
          <span className="truncate">{message.text}</span>
        </div>
      )}
    </div>
  )
}

export function SoftkeyBar({ keys, onPress }: { keys: (Softkey | null)[]; onPress: (k: Softkey) => void }) {
  return (
    <div className="grid shrink-0 grid-cols-8 gap-1.5" role="toolbar" aria-label="Функциональные клавиши">
      {keys.map((k, i) => {
        const label = `F${i + 1}`
        if (!k) {
          return (
            <div
              key={label}
              className="flex h-[52px] flex-col rounded-md border border-dashed bg-muted/50 p-1.5"
              aria-hidden="true"
            >
              <Keycap className="h-5 min-w-5 px-1 text-[10px] opacity-40">{label}</Keycap>
            </div>
          )
        }
        const Icon = k.icon
        return (
          <button
            key={label}
            type="button"
            tabIndex={-1}
            disabled={k.disabled}
            aria-pressed={k.active ?? undefined}
            onClick={() => onPress(k)}
            className={cn(
              'relative flex h-[52px] flex-col justify-between overflow-hidden rounded-md border bg-card p-1.5 text-left transition-colors hover:bg-accent disabled:cursor-not-allowed disabled:opacity-40 disabled:hover:bg-card',
              k.active && 'border-primary bg-accent',
            )}
          >
            {k.active && <span className="absolute inset-x-0 top-0 h-0.5 bg-success" aria-hidden="true" />}
            <div className="flex items-center justify-between">
              <Keycap className="h-5 min-w-5 px-1 text-[10px]">{label}</Keycap>
              <Icon className={cn('size-4', k.active ? 'text-primary' : 'text-muted-foreground')} aria-hidden="true" />
            </div>
            <span className="truncate text-xs font-semibold leading-tight">{k.label}</span>
          </button>
        )
      })}
    </div>
  )
}
