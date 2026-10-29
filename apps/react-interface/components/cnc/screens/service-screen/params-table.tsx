import { useEffect, useRef } from 'react'
import { ChevronLeft, ChevronRight, Lock, LockOpen, RotateCcw } from 'lucide-react'
import { formatParam, formatValue, jointGroup, paramValue, type ParamDef } from '@/lib/cnc/service-data'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap } from '../../primitives'

export function AxisSelector({ state }: { state: MachineState }) {
  return (
    <div className="flex shrink-0 items-center gap-2 border-b px-3 py-2">
      <span className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">Ось</span>
      {AXES.map((a) => {
        const active = a === state.service.axis
        return (
          <span
            key={a}
            aria-current={active ? 'true' : undefined}
            className={cn(
              'flex h-8 items-center gap-2 rounded-sm border px-2.5 font-mono text-sm font-bold',
              active ? 'border-primary bg-primary text-primary-foreground' : 'bg-muted',
            )}
          >
            {a}
            <span
              className={cn(
                'font-sans text-[11px] font-medium',
                active ? 'text-primary-foreground/80' : 'text-muted-foreground',
              )}
            >
              {jointGroup(a)}
            </span>
          </span>
        )
      })}
      <span className="ml-auto flex items-center gap-1.5 text-xs text-muted-foreground">
        <Keycap>←</Keycap>
        <Keycap>→</Keycap>
        выбор оси
      </span>
    </div>
  )
}

export const HEADER_CELL =
  'px-3 py-1.5 text-left text-[11px] font-semibold uppercase tracking-wider text-muted-foreground'

export function ParamTable({ state, defs }: { state: MachineState; defs: readonly ParamDef[] }) {
  const cursor = state.cursor.service
  const selectedRef = useRef<HTMLTableRowElement>(null)
  const { section, axis, focus, editing, saved, write } = state.service

  useEffect(() => {
    selectedRef.current?.scrollIntoView({ block: 'nearest' })
  }, [cursor, section, axis, focus])

  return (
    <div className="min-h-0 flex-1 overflow-y-auto">
      <table className="w-full table-fixed border-collapse">
        <thead className="sticky top-0 z-10">
          <tr className="border-b bg-card">
            <th className={cn(HEADER_CELL, 'w-[31%]')}>Параметр</th>
            <th className={cn(HEADER_CELL, 'w-[24%]')}>Ключ INI</th>
            <th className={cn(HEADER_CELL, 'w-[25%] text-center')}>Значение</th>
            <th className={cn(HEADER_CELL, 'w-[20%] text-right')}>По умолч.</th>
          </tr>
        </thead>
        <tbody>
          {defs.map((def, i) => {
            const value = paramValue(state.params, def.id)
            const selected = i === cursor && focus === 'list'
            const isEditing = editing?.id === def.id
            const changed = value !== def.default
            const unsaved = value !== saved[def.id]
            const locked = !def.free && !write
            const defaultValue = formatValue(def, def.default)
            return (
              <tr
                key={def.id}
                ref={selected ? selectedRef : undefined}
                aria-selected={selected}
                className={cn(
                  'scroll-mt-8 border-b',
                  selected && 'bg-accent outline-2 -outline-offset-2 outline-primary',
                  isEditing && 'bg-primary/10 outline-warning',
                )}
              >
                <td className="px-3 py-2">
                  <div className="flex min-w-0 items-center gap-2">
                    <span className="line-clamp-2 min-w-0 text-xs font-semibold leading-snug" title={def.label}>
                      {def.label}
                    </span>
                    <span className="ml-auto flex w-16 shrink-0 items-center justify-end gap-1.5">
                      {def.restart && (
                        <RotateCcw
                          className="size-3.5 shrink-0 text-muted-foreground"
                          aria-label="Требует перезапуска"
                        />
                      )}
                      {locked ? (
                        <Lock className="size-3.5 shrink-0 text-muted-foreground" aria-label="Запись заблокирована" />
                      ) : (
                        <LockOpen className="size-3.5 shrink-0 text-success" aria-label="Доступно для записи" />
                      )}
                      {unsaved ? (
                        <span
                          role="img"
                          aria-label="Не сохранено"
                          title="Не сохранено"
                          className="size-2 shrink-0 rounded-full bg-warning"
                        />
                      ) : (
                        <span className="size-2 shrink-0" aria-hidden="true" />
                      )}
                    </span>
                  </div>
                </td>
                <td
                  className="px-3 py-2 font-mono text-[11px] text-muted-foreground"
                  title={`[${def.group}] ${def.key}`}
                >
                  <span className="line-clamp-2 break-words">
                    [{def.group}] {def.key}
                  </span>
                </td>
                <td className="px-3 py-1.5">
                  <div className="flex items-center justify-center gap-1">
                    <ChevronLeft
                      className={cn('size-4 shrink-0', isEditing ? 'text-warning' : 'text-transparent')}
                      aria-hidden="true"
                    />
                    <span
                      className={cn(
                        'min-w-24 rounded-sm border px-2 py-0.5 text-center font-mono text-sm font-bold tabular-nums',
                        changed ? 'border-primary/40 bg-primary/10 text-primary' : 'bg-muted',
                        isEditing && 'border-warning bg-warning text-warning-foreground ring-2 ring-warning/40',
                      )}
                    >
                      {formatParam(def, state.params)}
                      {def.unit && (
                        <span
                          className={cn(
                            'ml-1 font-sans text-[11px] font-medium',
                            isEditing ? 'text-warning-foreground/80' : 'text-muted-foreground',
                          )}
                        >
                          {def.unit}
                        </span>
                      )}
                    </span>
                    <ChevronRight
                      className={cn('size-4 shrink-0', isEditing ? 'text-warning' : 'text-transparent')}
                      aria-hidden="true"
                    />
                  </div>
                </td>
                <td
                  className="px-3 py-2 text-right font-mono text-xs tabular-nums text-muted-foreground"
                  title={defaultValue}
                >
                  <span className="line-clamp-2 break-words">{defaultValue}</span>
                </td>
              </tr>
            )
          })}
        </tbody>
      </table>
    </div>
  )
}
