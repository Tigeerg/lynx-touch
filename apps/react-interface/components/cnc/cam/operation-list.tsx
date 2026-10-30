'use client'

// The operation cards of the quick program (PLAN-QUICKPROG, 7.4, C5 and 7.6): the number, the kind,
// the tool and the two numbers the operator checks on the drawing, then the warning if the operation
// does not pass its own checks. Every action is a button, so P3 needs no keyboard and P1 reaches it
// by softkey focus.
//
// The list itself is this file: the empty line, the entries (`operation-card.tsx`) and one of the
// copying dialogs (`copy-dialogs.tsx`) over the list. One dialog per list, not per card: only one
// card opens a dialog at a time, and it is drawn once, not once per entry.
import { Plus } from 'lucide-react'
import { useState, type Dispatch } from 'react'
import type { CamAction } from '@/lib/cnc/cam/cam'
import type { QuickProgram } from '@/lib/cnc/cam/ops'
import { cn } from '@/lib/utils'
import { useUiMode } from '../ui-mode-context'
import { ArrayDialog, RotateDialog } from './copy-dialogs'
import { OperationCard } from './operation-card'

type CopyDialogKind = 'array' | 'rotate' | null

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
  // The glyph of a button: bigger in the touch variant, the target itself is set by `target`.
  const icon = touch ? 'size-6' : 'size-4'
  const ops = program.operations
  // One dialog per list, not per card: only one card is open at a time, and the dialog is drawn
  // once over the list instead of once per entry.
  const [dialog, setDialog] = useState<{ kind: Exclude<CopyDialogKind, null>; index: number } | null>(null)

  return (
    <div className="relative flex min-h-0 flex-1 flex-col gap-1.5">
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
            onCopy={(kind) => setDialog({ kind, index: i })}
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
      {dialog !== null &&
        (dialog.kind === 'array' ? (
          <ArrayDialog index={dialog.index} dispatch={dispatch} onClose={() => setDialog(null)} />
        ) : (
          <RotateDialog index={dialog.index} dispatch={dispatch} onClose={() => setDialog(null)} />
        ))}
    </div>
  )
}
