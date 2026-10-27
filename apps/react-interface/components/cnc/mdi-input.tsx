'use client'

import { CornerDownLeft, Delete, Keyboard, Play, X } from 'lucide-react'
import { useState } from 'react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Overlay } from './overlay'
import { useUiMode } from './ui-mode-context'

const EXAMPLES = ['G0 X0 Y0', 'G0 Z50', 'S1500 M3', 'M5', 'G1 X10 F300', 'G28']

const KEY_ROWS = ['1234567890', 'QWERTYUIOP', 'ASDFGHJKL', 'ZXCVBNM']

// The touch MDI keyboard: a full QWERTY face keeps the familiar layout, while the G-code
// letters are the ones used most. The buffer is edited through the same MDI_BUFFER action as
// the physical keyboard, so the machine sees no second input path.
function TouchMdiKeyboard({
  state,
  dispatch,
  onClose,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  onClose: () => void
}) {
  const { buffer, running } = state.mdi
  const busy = state.run === 'RUN' || state.run === 'HOLD'
  const type = (value: string) => dispatch({ type: 'MDI_BUFFER', value })
  const append = (key: string) => type(buffer + key)
  const backspace = () => type(buffer.slice(0, -1))

  const special =
    'flex min-h-11 items-center justify-center rounded-md border border-b-[3px] bg-card px-2 text-sm font-semibold active:translate-y-px active:border-b active:bg-accent'

  return (
    <Overlay onClose={onClose} label="Экранная клавиатура MDI" className="w-full max-w-3xl border-2">
      <header className="flex items-center gap-2 border-b bg-muted px-3 py-2">
        <Keyboard className="size-4 text-primary" aria-hidden="true" />
        <h2 className="text-sm font-bold uppercase tracking-wide">Ввод MDI</h2>
        <output className="ml-2 min-w-0 flex-1 truncate rounded-sm border bg-background px-2 py-1 font-mono text-lg font-normal uppercase">
          {buffer || '—'}
        </output>
        <button
          type="button"
          tabIndex={-1}
          aria-label="Закрыть клавиатуру"
          onClick={onClose}
          className="rounded p-1.5 hover:bg-accent"
        >
          <X className="size-5" aria-hidden="true" />
        </button>
      </header>

      <div className="flex flex-col gap-1.5 p-3">
        {KEY_ROWS.map((row) => (
          <div key={row} className="grid grid-cols-10 gap-1.5">
            {[...row].map((key) => (
              <button
                key={key}
                type="button"
                tabIndex={-1}
                disabled={busy}
                onClick={() => append(key)}
                className="min-h-11 rounded-md border border-b-[3px] bg-card font-mono text-base font-bold active:translate-y-px active:border-b active:bg-accent disabled:opacity-40"
              >
                {key}
              </button>
            ))}
          </div>
        ))}

        <div className="grid grid-cols-10 gap-1.5">
          <button type="button" tabIndex={-1} disabled={busy} onClick={() => append('.')} className={special}>
            .
          </button>
          <button type="button" tabIndex={-1} disabled={busy} onClick={() => append('−')} className={special}>
            −
          </button>
          <button
            type="button"
            tabIndex={-1}
            disabled={busy}
            onClick={() => append(' ')}
            className={cn(special, 'col-span-2')}
          >
            Пробел
          </button>
          <button
            type="button"
            tabIndex={-1}
            disabled={busy || !buffer}
            onClick={backspace}
            aria-label="Удалить символ"
            className={special}
          >
            <Delete className="size-5" aria-hidden="true" />
          </button>
          <button
            type="button"
            tabIndex={-1}
            disabled={busy || !buffer}
            onClick={() => type('')}
            className={cn(special, 'col-span-2')}
          >
            Очистить
          </button>
          <button
            type="button"
            tabIndex={-1}
            disabled={busy || !buffer.trim()}
            onClick={() => {
              dispatch({ type: 'CYCLE_START' })
              onClose()
            }}
            className={cn(special, 'col-span-2 border-primary bg-primary text-primary-foreground')}
          >
            <Play className="mr-1 size-4" aria-hidden="true" />
            Пуск
          </button>
          <button type="button" tabIndex={-1} onClick={onClose} className={cn(special, 'col-span-2')}>
            <CornerDownLeft className="mr-1 size-4" aria-hidden="true" />
            Закрыть
          </button>
        </div>
      </div>
    </Overlay>
  )
}

export function MdiInput({
  state,
  dispatch,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  className?: string
}) {
  const { buffer, history, running } = state.mdi
  const busy = state.run === 'RUN' || state.run === 'HOLD'
  const touch = useUiMode()?.mode === 'touch'
  const [keyboard, setKeyboard] = useState(false)
  const recent = history.slice(0, 3)

  return (
    <section aria-label="Командная строка MDI" className={cn('flex min-h-0 flex-col gap-1.5', className)}>
      <div
        className={cn(
          'flex min-h-0 flex-1 flex-col gap-1.5 rounded-md border-2 bg-card p-2',
          running ? 'border-success' : busy ? 'border-warning' : 'border-primary',
        )}
      >
        <div className="flex items-center justify-between text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
          <span className="flex items-center gap-1.5">
            <Keyboard className="size-3.5" aria-hidden="true" />
            {touch ? 'Экранная клавиатура MDI' : 'Ввод с клавиатуры стойки'}
          </span>
          <span role="status" className={cn(running ? 'text-success' : busy && 'text-warning-foreground')}>
            {running ? 'Выполняется' : busy ? 'Станок занят' : touch ? 'Коснитесь строки' : 'Ожидание кадра'}
          </span>
        </div>

        <div
          className={cn(
            'flex items-center gap-3 rounded-sm bg-muted px-3 py-1.5',
            touch && !busy && 'cursor-pointer active:bg-accent',
          )}
          role={touch ? 'button' : undefined}
          tabIndex={touch ? -1 : undefined}
          aria-label={touch ? 'Открыть экранную клавиатуру MDI' : undefined}
          onClick={touch && !busy ? () => setKeyboard(true) : undefined}
        >
          <span className="shrink-0 font-mono text-xl font-bold text-primary" aria-hidden="true">
            MDI&gt;
          </span>
          <output className="flex min-w-0 flex-1 items-center font-mono text-xl font-normal uppercase tracking-normal text-foreground">
            <span className="truncate whitespace-pre">{buffer}</span>
            {!busy && (
              <span className="animate-caret ml-0.5 inline-block h-5 w-3 shrink-0 bg-primary" aria-hidden="true" />
            )}
            {!buffer && busy && (
              <span className="truncate text-muted-foreground">{running ? (history[0] ?? '') : ''}</span>
            )}
          </output>
        </div>

        <div className="flex items-center justify-between gap-2">
          <div className="flex min-w-0 flex-wrap gap-1">
            {recent.length > 0
              ? recent.map((text, i) => (
                  <button
                    key={`${text}-${i}`}
                    type="button"
                    tabIndex={-1}
                    disabled={busy}
                    title="История команд"
                    onClick={() => dispatch({ type: 'MDI_RECALL', index: i })}
                    className="max-w-40 truncate rounded-sm border bg-background px-1.5 py-0.5 font-mono text-[11px] text-muted-foreground transition-colors hover:bg-accent hover:text-foreground disabled:opacity-40"
                  >
                    {text}
                  </button>
                ))
              : EXAMPLES.map((ex) => (
                  <button
                    key={ex}
                    type="button"
                    tabIndex={-1}
                    disabled={busy}
                    onClick={() => dispatch({ type: 'MDI_BUFFER', value: ex })}
                    className="rounded-sm border bg-background px-1.5 py-0.5 font-mono text-[11px] text-muted-foreground transition-colors hover:bg-accent hover:text-foreground disabled:opacity-40"
                  >
                    {ex}
                  </button>
                ))}
          </div>
          <div className="flex shrink-0 items-center gap-1.5">
            <button
              type="button"
              tabIndex={-1}
              disabled={busy || !buffer}
              onClick={() => dispatch({ type: 'MDI_BUFFER', value: '' })}
              className="flex h-8 items-center gap-1.5 rounded-md border bg-background px-2 text-xs font-semibold transition-colors hover:bg-accent disabled:opacity-40"
            >
              <Delete className="size-4" aria-hidden="true" />
              Очистить
            </button>
            <button
              type="button"
              tabIndex={-1}
              disabled={busy || !buffer.trim()}
              onClick={() => dispatch({ type: 'CYCLE_START' })}
              className="flex h-8 items-center gap-1.5 rounded-md bg-primary px-2.5 text-xs font-semibold text-primary-foreground transition-opacity hover:opacity-90 disabled:opacity-40"
            >
              <Play className="size-4" aria-hidden="true" />
              Пуск
            </button>
          </div>
        </div>
      </div>

      {keyboard && touch && <TouchMdiKeyboard state={state} dispatch={dispatch} onClose={() => setKeyboard(false)} />}
    </section>
  )
}
