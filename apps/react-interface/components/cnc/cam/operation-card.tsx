// One card of the quick-program list (PLAN-QUICKPROG, 7.4, C5 and 7.6): the number, the kind, the
// tool and the numbers the operator checks on the drawing, the warning of the kind as a sign, and
// one row of icon actions with the copies menu under it. The row is icons only: its words live in
// `title`/`aria-label`, because six targets of 44 px already fill the 276 px card.
'use client'

import { ArrowDown, ArrowUp, Ellipsis, Pencil, Power, Trash2, TriangleAlert } from 'lucide-react'
import { useState, type Dispatch } from 'react'
import type { CamAction } from '@/lib/cnc/cam/cam'
import type { Operation, QuickProgram } from '@/lib/cnc/cam/ops'
import { formProblems, valuesOf } from '@/lib/cnc/cam/form'
import { cn } from '@/lib/utils'
import { KIND_LABEL, kindWarnings, formatMm, keySize, toolText, warningText } from './cam-labels'
import { COPY_MENU } from './cam-dialogs'
import { smallButton } from './copy-dialogs'

export function OperationCard({
  op,
  index,
  count,
  program,
  editing,
  touch,
  icon,
  onCopy,
  dispatch,
}: {
  op: Operation
  index: number
  count: number
  program: QuickProgram
  editing: boolean
  touch: boolean
  icon: string
  onCopy: (kind: 'array' | 'rotate') => void
  dispatch: Dispatch<CamAction>
}) {
  const problem = formProblems(program, valuesOf(op), op)[0]
  const warnings = kindWarnings(op)
  const off = !op.enabled
  const [open, setOpen] = useState(false)
  const target = touch ? 'min-h-11 min-w-11' : 'h-6 px-1'

  return (
    <li
      aria-current={editing ? 'true' : undefined}
      className={cn(
        'relative flex flex-col gap-1 rounded-md border bg-card px-2 py-1.5',
        editing && 'border-primary ring-1 ring-primary',
        !op.enabled && 'opacity-70',
      )}
    >
      <div className="flex items-center gap-1.5">
        <span className="flex size-5 shrink-0 items-center justify-center rounded-sm bg-muted font-mono text-[10px] font-bold text-muted-foreground">
          {index + 1}
        </span>
        <span className="min-w-0 flex-1 truncate text-xs font-bold">{KIND_LABEL[op.kind]}</span>
        {warnings.length > 0 && (
          <span className="shrink-0 text-warning" title={warningText(warnings)} aria-label={warningText(warnings)}>
            <TriangleAlert className="size-3.5" aria-hidden="true" />
          </span>
        )}
        {off && <span className="shrink-0 text-[10px] font-semibold uppercase text-muted-foreground">выкл</span>}
      </div>
      <p className="truncate font-mono text-[10px] tabular-nums text-muted-foreground">
        {toolText(op)} · {keySize(op)} · глуб. {formatMm(op.depthMm)}
      </p>
      {problem && (
        <p className="flex items-start gap-1 text-[10px] leading-tight text-destructive" role="status">
          <TriangleAlert className="mt-px size-3 shrink-0" aria-hidden="true" />
          <span className="line-clamp-2">{problem.reason}</span>
        </p>
      )}
      {/* Six targets of 44 px do not fit the 276 px card column with a 4 px gap; a 2 px gap keeps
          them inside both on the panel and on the touch, where the words are already gone. */}
      <div className="flex flex-wrap items-center gap-x-0.5 gap-y-1">
        <button
          type="button"
          tabIndex={-1}
          aria-label={`Выше: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          disabled={index === 0}
          onClick={() => dispatch({ type: 'CAM_MOVE', index, up: true })}
          className={cn(smallButton, target)}
        >
          <ArrowUp className={icon} aria-hidden="true" />
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-label={`Ниже: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          disabled={index === count - 1}
          onClick={() => dispatch({ type: 'CAM_MOVE', index, up: false })}
          className={cn(smallButton, target)}
        >
          <ArrowDown className={icon} aria-hidden="true" />
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-pressed={op.enabled}
          // The word of the state lives in the label: six targets of 44 px plus the gaps are
          // 284 px of the 276 px card, so the row drops the words and keeps the icons only.
          aria-label={`${op.enabled ? 'Выключить' : 'Включить'}: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          title={op.enabled ? 'Выключить' : 'Включить'}
          onClick={() => dispatch({ type: 'CAM_TOGGLE', index })}
          className={cn(smallButton, target, op.enabled && 'border-success/60 text-success')}
        >
          <Power className={icon} aria-hidden="true" />
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-label={`Правка: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          title="Правка"
          onClick={() => dispatch({ type: 'CAM_EDIT', index })}
          className={cn(smallButton, target)}
        >
          <Pencil className={icon} aria-hidden="true" />
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-label={`Удалить: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          onClick={() => dispatch({ type: 'CAM_REMOVE', index })}
          className={cn(smallButton, target, 'ml-auto text-destructive')}
        >
          <Trash2 className={icon} aria-hidden="true" />
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-expanded={open}
          title="Копии: массив и поворот"
          aria-label={`Копии: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          onClick={() => setOpen(!open)}
          className={cn(smallButton, target, open && 'bg-accent')}
        >
          <Ellipsis className={icon} aria-hidden="true" />
        </button>
      </div>
      {open && (
        <div role="menu" aria-label="Копии операции" className="flex gap-1">
          {COPY_MENU.map(({ kind, label, icon: MenuIcon }) => (
            <button
              key={kind}
              type="button"
              role="menuitem"
              tabIndex={-1}
              onClick={() => {
                setOpen(false)
                onCopy(kind)
              }}
              className={cn(smallButton, 'flex-1', touch ? 'min-h-11' : 'h-7')}
            >
              <MenuIcon className={icon} aria-hidden="true" />
              {label}
            </button>
          ))}
        </div>
      )}
    </li>
  )
}
