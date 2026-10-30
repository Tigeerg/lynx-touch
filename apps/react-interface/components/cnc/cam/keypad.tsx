'use client'

// The on-screen input of the form in the touch variant (PLAN-QUICKPROG, 7.4, C5; PANEL P3).
//
// The panel keeps the machine's digit block and the keyboard; the touch panel has neither, so a
// numeric field gets a compact pad under the form and a choice field stays a list. The buffer is
// the form value itself: a key sends `CAM_PARAM` right away, so the preview follows each digit
// and there is no second copy of what the operator typed.
import { Delete } from 'lucide-react'
import type { Dispatch } from 'react'
import type { CamAction } from '@/lib/cnc/cam/cam'
import { cn } from '@/lib/utils'
import { CHOICES } from './cam-fields'

// `-` is the sign of a number as the parser reads it (`Number('-2')`); the minus glyph is not.
const KEYS = ['7', '8', '9', '4', '5', '6', '1', '2', '3', '.', '0', '-']

const keyClass =
  'flex min-h-11 items-center justify-center rounded-md border border-b-[3px] bg-card font-mono text-base font-bold active:translate-y-px active:border-b active:bg-accent'

/** The field the pad edits; `null` when the operator has not picked a number yet. */
export function Keypad({
  field,
  value,
  dispatch,
}: {
  field: string | null
  value: string | null
  dispatch: Dispatch<CamAction>
}) {
  // A choice field is picked from its list; the pad would only type into a value the list rejects.
  if (field === null || value === null || CHOICES[field] !== undefined) {
    return (
      <p className="shrink-0 rounded-md border border-dashed px-2 py-1.5 text-[10px] text-muted-foreground">
        Коснитесь числа в форме — под ней появится цифровой блок
      </p>
    )
  }
  const send = (next: string) => dispatch({ type: 'CAM_PARAM', field, value: next })

  return (
    <div role="group" aria-label="Цифровой блок формы" className="grid shrink-0 grid-cols-6 gap-1">
      {KEYS.map((k) => (
        <button key={k} type="button" tabIndex={-1} onClick={() => send(value + k)} className={keyClass}>
          {k}
        </button>
      ))}
      <button
        type="button"
        tabIndex={-1}
        aria-label="Стереть символ"
        onClick={() => send(value.slice(0, -1))}
        className={keyClass}
      >
        <Delete className="size-4" aria-hidden="true" />
      </button>
      <button type="button" tabIndex={-1} onClick={() => send('')} className={cn(keyClass, 'col-span-2 text-xs')}>
        Очистить
      </button>
    </div>
  )
}
