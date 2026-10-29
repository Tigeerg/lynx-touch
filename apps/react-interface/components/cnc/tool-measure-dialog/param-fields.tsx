// The sensor parameters: the feeds, the travel and the tolerance the frame uses, plus the tool
// the dialog measures. Static data, no state.
import { MEASURE_PARAMS } from '@/lib/cnc/tool-measure'
import type { Tool } from '@/lib/cnc/types'

function Param({ label, value, unit }: { label: string; value: string; unit?: string }) {
  return (
    <div className="flex flex-col gap-0.5">
      <span className="text-[11px] text-muted-foreground">{label}</span>
      <span className="flex items-baseline justify-between border bg-muted px-2 py-1 font-mono text-sm font-bold tabular-nums">
        {value}
        {unit && <span className="text-[10px] font-normal text-muted-foreground">{unit}</span>}
      </span>
    </div>
  )
}

export function ParamFields({ tool }: { tool: Tool }) {
  return (
    <section aria-label="Параметры датчика" className="flex flex-col gap-1 bg-card p-3">
      <h3 className="mb-1 text-[11px] font-bold uppercase tracking-wider text-muted-foreground">Параметры</h3>
      <Param label="Быстрый подвод" value={MEASURE_PARAMS.rapidFeed.toFixed(1)} unit="мм/мин" />
      <Param label="Медл. подвод" value={MEASURE_PARAMS.fineFeed.toFixed(1)} unit="мм/мин" />
      <Param label="Ход по Z" value={MEASURE_PARAMS.zTravel.toFixed(1)} unit="мм" />
      <Param label="Отвод" value={MEASURE_PARAMS.retract.toFixed(1)} unit="мм" />
      <Param label="Допуск поломки" value={MEASURE_PARAMS.breakageTolerance.toFixed(1)} unit="мм" />
      <div className="mt-2 border-t pt-2">
        <Param label="Инструмент" value={`T${String(tool.number).padStart(2, '0')}`} />
        <Param label="Диаметр" value={tool.diameter.toFixed(3)} unit="мм" />
      </div>
    </section>
  )
}
