import { useEffect, useRef, type Dispatch } from 'react'
import { Search, X } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

// Find and replace bar: Enter finds, Shift-free Enter in the second field replaces all, Escape closes.
export function SearchBar({ state, dispatch }: { state: MachineState; dispatch: Dispatch<Action> }) {
  const findRef = useRef<HTMLInputElement>(null)
  useEffect(() => {
    findRef.current?.focus()
  }, [])

  const onKey = (e: { key: string; preventDefault: () => void }, enter: Action) => {
    if (e.key === 'Enter') {
      e.preventDefault()
      dispatch(enter)
    } else if (e.key === 'Escape') {
      e.preventDefault()
      dispatch({ type: 'SEARCH', open: false })
    }
  }
  const input =
    'min-w-0 rounded-sm border bg-background px-2 py-0.5 font-mono text-sm uppercase outline-none focus:border-primary'

  return (
    <div className="flex shrink-0 items-center gap-1.5 rounded-md border-2 border-primary bg-card px-2 py-1.5">
      <Search className="size-4 shrink-0 text-primary" aria-hidden="true" />
      <input
        ref={findRef}
        value={state.search.query}
        aria-label="Что искать в программе"
        placeholder="T5, M06, G1…"
        spellCheck={false}
        autoComplete="off"
        onChange={(e) => dispatch({ type: 'SEARCH_QUERY', value: e.target.value })}
        onKeyDown={(e) => onKey(e, { type: 'SEARCH_FIND' })}
        className={cn(input, 'flex-1')}
      />
      <span className="shrink-0 text-xs font-bold text-muted-foreground" aria-hidden="true">
        {'->'}
      </span>
      <input
        value={state.search.replace}
        aria-label="Заменить на"
        placeholder="замена…"
        spellCheck={false}
        autoComplete="off"
        onChange={(e) => dispatch({ type: 'SEARCH_REPLACE', value: e.target.value })}
        onKeyDown={(e) => onKey(e, { type: 'SEARCH_REPLACE_ALL' })}
        className={cn(input, 'w-32')}
      />
      <button
        type="button"
        tabIndex={-1}
        aria-label="Закрыть поиск"
        className="rounded p-1 hover:bg-accent"
        onClick={() => dispatch({ type: 'SEARCH', open: false })}
      >
        <X className="size-4" aria-hidden="true" />
      </button>
    </div>
  )
}
