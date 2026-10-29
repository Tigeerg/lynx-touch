import { useEffect, useRef, type Dispatch } from 'react'
import type { Action } from '@/lib/cnc/machine'

// In-place editor of one program block: Enter commits, Escape cancels, the field keeps focus.
export function LineEditor({ value, dispatch }: { value: string; dispatch: Dispatch<Action> }) {
  const ref = useRef<HTMLInputElement>(null)

  useEffect(() => {
    ref.current?.focus()
  }, [])

  return (
    <input
      ref={ref}
      value={value}
      aria-label="Редактирование кадра"
      spellCheck={false}
      autoComplete="off"
      onChange={(e) => dispatch({ type: 'EDIT_BUFFER', value: e.target.value })}
      onBlur={() => ref.current?.focus()}
      onKeyDown={(e) => {
        if (e.nativeEvent.isComposing || e.keyCode === 229) return
        if (e.key === 'Enter') {
          e.preventDefault()
          dispatch({ type: 'EDIT_COMMIT' })
        } else if (e.key === 'Escape') {
          e.preventDefault()
          dispatch({ type: 'EDIT_CANCEL' })
        }
      }}
      className="min-w-0 flex-1 rounded-sm border-2 border-primary bg-card px-2 py-0.5 font-mono text-base uppercase outline-none"
    />
  )
}
