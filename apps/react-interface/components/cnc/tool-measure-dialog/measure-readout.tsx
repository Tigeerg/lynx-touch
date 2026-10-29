// The right column of the dialog: the phase, the progress bar and the numeric readout of the
// length the sensor has measured so far.
import { MEASURE_PARAMS, MEASURE_TOTAL_S, type MeasureFrame } from '@/lib/cnc/tool-measure'
import type { Tool } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { PHASE_LABEL } from './phase-label'

function Readout({ label, value, tone }: { label: string; value: string; tone?: string }) {
  return (
    <div className="flex items-baseline justify-between border bg-muted px-2 py-1">
      <dt className="font-sans text-[11px] text-muted-foreground">{label}</dt>
      <dd className={cn('text-sm font-bold', tone)}>{value}</dd>
    </div>
  )
}

/** `zNow` is the spindle Z the dialog derives from the frame, `elapsed` is the run time so far. */
export function MeasureReadout({
  frame,
  tool,
  zNow,
  elapsed,
  length,
}: {
  frame: MeasureFrame
  tool: Tool
  zNow: number
  elapsed: number
  length: number | null
}) {
  const delta = length !== null ? length - tool.length : null
  return (
    <section aria-label="Ход замера" className="flex flex-col gap-2 bg-card p-3">
      <h3 className="text-[11px] font-bold uppercase tracking-wider text-muted-foreground">Состояние</h3>
      <p
        aria-live="polite"
        className={cn(
          'border px-2 py-1.5 text-center font-mono text-xs font-bold uppercase',
          frame.triggered && 'border-warning bg-warning text-warning-foreground',
          frame.phase === 'done' && 'border-success bg-success text-primary-foreground',
        )}
      >
        {PHASE_LABEL[frame.phase]}
      </p>

      <div
        role="progressbar"
        aria-label="Ход замера"
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(frame.progress * 100)}
        className="h-3 overflow-hidden border bg-muted"
      >
        <div
          className="h-full bg-primary transition-[width] duration-75"
          style={{ width: `${frame.progress * 100}%` }}
        />
      </div>
      <p className="text-right font-mono text-[11px] tabular-nums text-muted-foreground">
        {Math.min(elapsed, MEASURE_TOTAL_S).toFixed(1)} / {MEASURE_TOTAL_S.toFixed(1)} с
      </p>

      <dl className="flex flex-col gap-1 font-mono tabular-nums">
        <Readout label="Z шпинделя" value={zNow.toFixed(3)} />
        <Readout
          label="Длина"
          value={length === null ? '—' : length.toFixed(3)}
          tone={length === null ? undefined : 'text-success'}
        />
        <Readout label="Было" value={tool.length.toFixed(3)} />
        <Readout
          label="Δ"
          value={delta === null ? '—' : `${delta >= 0 ? '+' : ''}${delta.toFixed(3)}`}
          tone={
            delta === null
              ? undefined
              : Math.abs(delta) > MEASURE_PARAMS.breakageTolerance
                ? 'text-destructive'
                : 'text-success'
          }
        />
      </dl>
    </section>
  )
}
