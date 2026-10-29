import { CircleAlert, CircleCheck } from 'lucide-react'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { formatCoord } from '../../primitives'

// Machine axes next to the scheme.
export function Readout({ state }: { state: MachineState }) {
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

// Titled box of rows.
export function Spec({ title, children }: { title?: string; children: React.ReactNode }) {
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

// One check or value row: ok === true / false picks the icon, undefined leaves the row neutral.
export function Row({ label, value, ok }: { label: string; value?: string; ok?: boolean }) {
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
