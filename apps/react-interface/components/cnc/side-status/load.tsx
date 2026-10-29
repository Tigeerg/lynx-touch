import { RAPID_STEPS } from '@/lib/cnc/data'
import type { Axis } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap } from '../primitives'
import { STEP_KEY } from './sections'

export function LoadRow({ value }: { value: number }) {
  const tone = value > 80 ? 'bg-destructive' : value > 60 ? 'bg-warning' : 'bg-primary'
  return (
    <div className="flex items-center gap-2 text-xs">
      <span className="w-6 font-semibold text-muted-foreground">Нгр</span>
      <div
        role="meter"
        aria-label="Нагрузка на шпиндель (имитация)"
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.min(100, value)}
        className="relative h-3 flex-1 overflow-hidden rounded-sm border bg-muted"
      >
        <div
          className={cn('h-full transition-[width,background-color] duration-300', tone)}
          style={{ width: `${Math.min(100, value)}%` }}
        />
        <span className="absolute inset-y-0 left-[60%] w-px bg-foreground/30" aria-hidden="true" />
        <span className="absolute inset-y-0 left-[80%] w-px bg-foreground/30" aria-hidden="true" />
      </div>
      <span
        className={cn('w-10 text-right font-mono text-sm font-bold tabular-nums', value > 80 && 'text-destructive')}
      >
        {value}%
      </span>
    </div>
  )
}

// The load of one axis: the same bar in SETUP and AUTO, so the two sides read alike. The number
// comes from the library model (axisLoad), not from a local formula.
export function AxisLoad({ axis, value }: { axis: Axis; value: number }) {
  const tone = value > 80 ? 'bg-destructive' : value > 60 ? 'bg-warning' : 'bg-primary'
  return (
    <div className="flex items-center gap-2 text-xs">
      <span className="w-4 font-mono font-bold text-muted-foreground">{axis}</span>
      <div
        role="meter"
        aria-label={`Нагрузка оси ${axis}`}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.min(100, value)}
        className="relative h-2.5 flex-1 overflow-hidden rounded-sm border bg-muted"
      >
        <div
          className={cn('h-full transition-[width] duration-300', tone)}
          style={{ width: `${Math.min(100, value)}%` }}
        />
      </div>
      <span className="w-9 text-right font-mono text-xs font-bold tabular-nums">{value}%</span>
    </div>
  )
}

export function RapidRow({
  value,
  onStep,
  touch = false,
}: {
  value: number
  /** Present on the touch deck: the arrows step through 5/25/50/100 in the same order as the keys. */
  onStep?: (dir: 1 | -1) => void
  touch?: boolean
}) {
  return (
    <div className={cn('flex items-center gap-2', touch ? 'text-sm' : 'text-xs')}>
      <span className={cn('shrink-0 font-semibold text-muted-foreground', touch ? 'w-8' : 'w-6')}>G0</span>
      {onStep ? (
        <button
          type="button"
          tabIndex={-1}
          aria-label="Ускорение G0 меньше"
          onClick={() => onStep(-1)}
          className={STEP_KEY}
        >
          &lt;
        </button>
      ) : (
        <Keycap className="h-5 min-w-5 px-1 text-[10px]">9</Keycap>
      )}
      <div className="flex flex-1 overflow-hidden rounded-sm border">
        {RAPID_STEPS.map((r) => (
          <span
            key={r}
            className={cn(
              'flex flex-1 items-center justify-center text-center font-mono font-bold',
              touch ? 'py-1.5 text-sm' : 'py-0.5 text-xs',
              r === value ? 'bg-primary text-primary-foreground' : 'bg-muted text-muted-foreground',
            )}
          >
            {r}
          </span>
        ))}
      </div>
      {onStep ? (
        <button
          type="button"
          tabIndex={-1}
          aria-label="Ускорение G0 больше"
          onClick={() => onStep(1)}
          className={STEP_KEY}
        >
          &gt;
        </button>
      ) : (
        <Keycap className="h-5 min-w-5 px-1 text-[10px]">0</Keycap>
      )}
    </div>
  )
}
