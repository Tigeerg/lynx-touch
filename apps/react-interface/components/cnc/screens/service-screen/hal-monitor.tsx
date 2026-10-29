import { useEffect, useRef } from 'react'
import { HAL_GROUPS, type HalPinType } from '@/lib/cnc/service-data'
import { halValues } from '@/lib/cnc/hal'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { HEADER_CELL } from './params-table'

function PinValue({ type, value }: { type: HalPinType; value: boolean | number }) {
  if (type === 'bit') {
    return (
      <span
        className={cn(
          'inline-flex w-14 justify-center rounded-sm px-1.5 py-0.5 font-mono text-xs font-bold',
          value ? 'bg-success text-primary-foreground' : 'bg-muted text-muted-foreground',
        )}
      >
        {value ? 'TRUE' : 'FALSE'}
      </span>
    )
  }
  const n = Number(value)
  return <span className="font-mono text-sm font-semibold tabular-nums">{type === 's32' ? n : n.toFixed(4)}</span>
}

export function HalMonitor({ state }: { state: MachineState }) {
  const cursor = state.cursor.service
  const selectedRef = useRef<HTMLTableRowElement>(null)
  const listFocused = state.service.focus === 'list'
  const values = halValues(state)

  useEffect(() => {
    selectedRef.current?.scrollIntoView({ block: 'nearest' })
  }, [cursor, listFocused])

  let rowIndex = 0
  return (
    <div className="min-h-0 flex-1 overflow-y-auto">
      <table className="w-full border-collapse">
        <thead className="sticky top-0 z-10">
          <tr className="border-b bg-card">
            <th className={HEADER_CELL}>Пин HAL</th>
            <th className={cn(HEADER_CELL, 'w-20')}>Тип</th>
            <th className={cn(HEADER_CELL, 'w-20')}>Напр.</th>
            <th className={cn(HEADER_CELL, 'w-32 text-right')}>Значение</th>
          </tr>
        </thead>
        {HAL_GROUPS.map((group) => (
          <tbody key={group.title}>
            <tr className="border-b bg-muted">
              <th
                colSpan={4}
                scope="colgroup"
                className="px-3 py-1.5 text-left text-xs font-bold uppercase tracking-wider text-foreground"
              >
                {group.title}
                <span className="ml-2 font-mono font-normal normal-case text-muted-foreground">
                  {group.pins.length}
                </span>
              </th>
            </tr>
            {group.pins.map((pin) => {
              const selected = rowIndex === cursor && listFocused
              rowIndex += 1
              return (
                <tr
                  key={pin.name}
                  ref={selected ? selectedRef : undefined}
                  aria-selected={selected}
                  className={cn(
                    'scroll-mt-8 border-b',
                    selected && 'bg-accent outline-2 -outline-offset-2 outline-primary',
                  )}
                >
                  <td className="px-3 py-1.5 font-mono text-xs">
                    {pin.name}
                    {pin.note && <span className="ml-2 font-sans text-muted-foreground">{pin.note}</span>}
                  </td>
                  <td className="px-3 py-1.5 font-mono text-xs text-muted-foreground">{pin.type}</td>
                  <td className="px-3 py-1.5 font-mono text-xs text-muted-foreground">{pin.dir}</td>
                  <td className="px-3 py-1.5 text-right">
                    <PinValue type={pin.type} value={values[pin.name]} />
                  </td>
                </tr>
              )
            })}
          </tbody>
        ))}
      </table>
    </div>
  )
}
