'use client'

// The operation cards of the quick program (PLAN-QUICKPROG, 7.4, C5): the number, the kind, the
// tool and the two numbers the operator checks on the drawing, then the warning if the operation
// does not pass its own checks. The order, the on/off state, the edit and the delete are buttons,
// so P3 needs no keyboard and P1 reaches every action by softkey focus.
import { ArrowDown, ArrowUp, Pencil, Plus, Power, Trash2, TriangleAlert } from 'lucide-react'
import type { Dispatch } from 'react'
import type { CamAction } from '@/lib/cnc/cam/cam'
import type { Operation, QuickProgram } from '@/lib/cnc/cam/ops'
import { formProblems } from '@/lib/cnc/cam/form'
import { cn } from '@/lib/utils'
import { useUiMode } from '../ui-mode-context'
import { formatMm, KIND_LABEL, keySize, toolText } from './cam-labels'

const smallButton =
  'flex items-center justify-center gap-1 rounded-sm border bg-background px-1.5 text-[10px] font-semibold transition-colors hover:bg-accent active:bg-accent disabled:opacity-40'

export function OperationList({
  program,
  editing,
  dispatch,
}: {
  program: QuickProgram
  editing: number | null
  dispatch: Dispatch<CamAction>
}) {
  const touch = useUiMode()?.mode === 'touch'
  const icon = touch ? 'size-5' : 'size-4'
  const ops = program.operations

  return (
    <div className="flex min-h-0 flex-1 flex-col gap-1.5">
      <div className="flex shrink-0 items-baseline justify-between">
        <h3 className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">Операции</h3>
        <span className="font-mono text-[10px] text-muted-foreground">{ops.length}</span>
      </div>
      <ol className="flex min-h-0 flex-1 flex-col gap-1.5 overflow-y-auto pr-0.5" aria-label="Список операций">
        {ops.length === 0 && (
          <li className="rounded-md border border-dashed p-3 text-center text-[11px] text-muted-foreground">
            Программа пуста: добавьте первую операцию
          </li>
        )}
        {ops.map((op, i) => (
          <OperationCard
            key={op.id || `${op.kind}-${String(i)}`}
            op={op}
            index={i}
            count={ops.length}
            program={program}
            editing={editing === i}
            touch={touch}
            icon={icon}
            dispatch={dispatch}
          />
        ))}
      </ol>
      <button
        type="button"
        tabIndex={-1}
        onClick={() => dispatch({ type: 'CAM_CANCEL' })}
        className={cn(
          'flex shrink-0 items-center justify-center gap-1.5 rounded-md border-2 border-dashed bg-card text-xs font-bold text-primary transition-colors hover:bg-accent',
          touch ? 'min-h-12' : 'min-h-9',
        )}
      >
        <Plus className={icon} aria-hidden="true" />
        Добавить операцию
      </button>
    </div>
  )
}

function OperationCard({
  op,
  index,
  count,
  program,
  editing,
  touch,
  icon,
  dispatch,
}: {
  op: Operation
  index: number
  count: number
  program: QuickProgram
  editing: boolean
  touch: boolean
  icon: string
  dispatch: Dispatch<CamAction>
}) {
  const problem = formProblems(program, op)[0]
  const off = !op.enabled
  return (
    <li
      aria-current={editing ? 'true' : undefined}
      className={cn(
        'flex flex-col gap-1 rounded-md border bg-card px-2 py-1.5',
        editing && 'border-primary ring-1 ring-primary',
        !op.enabled && 'opacity-70',
      )}
    >
      <div className="flex items-center gap-1.5">
        <span className="flex size-5 shrink-0 items-center justify-center rounded-sm bg-muted font-mono text-[10px] font-bold text-muted-foreground">
          {index + 1}
        </span>
        <span className="min-w-0 flex-1 truncate text-xs font-bold">{KIND_LABEL[op.kind]}</span>
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
      <div className="flex flex-wrap items-center gap-1">
        <button
          type="button"
          tabIndex={-1}
          aria-label={`Выше: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          disabled={index === 0}
          onClick={() => dispatch({ type: 'CAM_MOVE', index, up: true })}
          className={cn(smallButton, touch ? 'min-h-11 min-w-11' : 'h-6 px-1')}
        >
          <ArrowUp className={icon} aria-hidden="true" />
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-label={`Ниже: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          disabled={index === count - 1}
          onClick={() => dispatch({ type: 'CAM_MOVE', index, up: false })}
          className={cn(smallButton, touch ? 'min-h-11 min-w-11' : 'h-6 px-1')}
        >
          <ArrowDown className={icon} aria-hidden="true" />
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-pressed={op.enabled}
          onClick={() => dispatch({ type: 'CAM_TOGGLE', index })}
          className={cn(
            smallButton,
            touch ? 'min-h-11 min-w-11' : 'h-6 px-1',
            op.enabled && 'border-success/60 text-success',
          )}
        >
          <Power className={icon} aria-hidden="true" />
          {op.enabled ? 'Вкл' : 'Выкл'}
        </button>
        <button
          type="button"
          tabIndex={-1}
          onClick={() => dispatch({ type: 'CAM_EDIT', index })}
          className={cn(smallButton, touch ? 'min-h-11 min-w-11' : 'h-6 px-1')}
        >
          <Pencil className={icon} aria-hidden="true" />
          Правка
        </button>
        <button
          type="button"
          tabIndex={-1}
          aria-label={`Удалить: ${KIND_LABEL[op.kind]} ${String(index + 1)}`}
          onClick={() => dispatch({ type: 'CAM_REMOVE', index })}
          className={cn(smallButton, touch ? 'min-h-11 min-w-11' : 'h-6 px-1', 'text-destructive')}
        >
          <Trash2 className={icon} aria-hidden="true" />
        </button>
      </div>
    </li>
  )
}
