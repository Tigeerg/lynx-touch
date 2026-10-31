'use client'

// The editable cells of the overlay header (PLAN-QUICKPROG, 5 steps 1-2): the name, the offset, the
// stock box and the safe Z as small inputs of the document.
import { useEffect, useState } from 'react'
import { cn } from '@/lib/utils'

export function HeaderInput({
  cell,
  touch,
  onCommit,
}: {
  cell: { field: string; label: string; value: string }
  touch: boolean
  onCommit: (value: string) => void
}) {
  const [text, setText] = useState(cell.value)
  // The document may change from another action (a save, a reset): the cell follows it.
  useEffect(() => setText(cell.value), [cell.value])
  const commit = () => {
    if (text !== cell.value) onCommit(text)
  }
  return (
    <input
      value={text}
      aria-label={cell.label}
      title={cell.label}
      spellCheck={false}
      autoComplete="off"
      inputMode={cell.field === 'name' ? undefined : 'decimal'}
      onChange={(e) => setText(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => {
        if (e.key === 'Enter') {
          commit()
          e.currentTarget.blur()
        }
      }}
      className={cn(
        'w-11 min-w-0 rounded-sm border bg-background px-1 font-mono text-[10px] tabular-nums outline-none focus:border-primary',
        cell.field === 'name' ? 'w-24 text-left' : 'text-right',
        touch ? 'min-h-11' : 'h-6',
      )}
    />
  )
}
