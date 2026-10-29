import type { Action } from '@/lib/cnc/machine'
import type { LadderForm } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap } from '../../primitives'

type Dispatch = (a: Action) => void

export function Field({
  label,
  value,
  index,
  dispatch,
  mono,
  autoFocus,
  className,
}: {
  label: string
  value: string
  index: number
  dispatch: Dispatch
  mono?: boolean
  autoFocus?: boolean
  className?: string
}) {
  return (
    <label className={cn('flex min-w-0 flex-col gap-0.5 text-[11px] font-semibold text-muted-foreground', className)}>
      {label}
      <input
        value={value}
        autoFocus={autoFocus}
        spellCheck={false}
        autoComplete="off"
        onChange={(e) => dispatch({ type: 'LADDER_FORM_SET', index, value: e.target.value })}
        onKeyDown={(e) => {
          if (e.key === 'Escape') {
            e.preventDefault()
            dispatch({ type: 'LADDER_FORM_CANCEL' })
          }
        }}
        className={cn(
          'h-8 rounded-sm border-2 border-primary bg-background px-2 text-sm text-foreground outline-none focus:ring-2 focus:ring-primary/40',
          mono && 'font-mono font-bold uppercase',
        )}
      />
    </label>
  )
}

export function FormPanel({
  form,
  title,
  dispatch,
  chips,
  preset,
}: {
  form: LadderForm
  title: string
  dispatch: Dispatch
  chips?: { addr: string; label: string }[]
  preset?: 'timer' | 'counter' | null
}) {
  const compare = form.kind === 'element' && form.compare
  return (
    <form
      onSubmit={(e) => {
        e.preventDefault()
        dispatch({ type: 'LADDER_FORM_COMMIT' })
      }}
      className="flex shrink-0 flex-col gap-1.5 border-t-2 border-primary bg-muted px-3 py-2"
    >
      <button type="submit" className="sr-only" tabIndex={-1}>
        Применить
      </button>
      <div className="flex items-end gap-2">
        <span className="pb-1.5 text-xs font-bold text-primary">{title}</span>
        {form.kind === 'element' ? (
          <>
            <Field
              label={compare ? 'Условие (напр. T0 >= 1)' : 'Адрес'}
              value={form.values[0]}
              index={0}
              dispatch={dispatch}
              mono
              autoFocus
              className={compare ? 'w-56' : 'w-24'}
            />
            <Field label="Подпись" value={form.values[1]} index={1} dispatch={dispatch} className="w-44" />
            {preset === 'timer' && (
              <Field label="Уставка, с" value={form.values[3]} index={3} dispatch={dispatch} mono className="w-24" />
            )}
            {preset === 'counter' && (
              <Field label="Уставка, шт" value={form.values[3]} index={3} dispatch={dispatch} mono className="w-24" />
            )}
            <Field label="Ступень" value={form.values[2]} index={2} dispatch={dispatch} className="min-w-0 flex-1" />
          </>
        ) : (
          <>
            <Field
              label="Имя файла (.lad)"
              value={form.values[0]}
              index={0}
              dispatch={dispatch}
              autoFocus
              className="w-52 [&_input]:font-mono"
            />
            <Field label="Описание" value={form.values[1]} index={1} dispatch={dispatch} className="min-w-0 flex-1" />
          </>
        )}
        <span className="flex shrink-0 items-center gap-1 pb-1.5 text-[11px] text-muted-foreground">
          <Keycap>Enter</Keycap> применить <Keycap>Esc</Keycap> отмена
        </span>
      </div>
      {chips && (
        <div role="group" aria-label="Сигналы" className="flex flex-wrap gap-1">
          {chips.map((c) => (
            <button
              key={c.addr}
              type="button"
              title={c.label}
              onMouseDown={(e) => e.preventDefault()}
              onClick={() => dispatch({ type: 'LADDER_PICK', addr: c.addr, label: c.label })}
              className={cn(
                'rounded-sm border px-1.5 py-0.5 font-mono text-[11px] font-bold',
                form.values[0].toUpperCase() === c.addr
                  ? 'border-primary bg-primary text-primary-foreground'
                  : 'bg-background hover:border-primary',
              )}
            >
              {c.addr}
            </button>
          ))}
        </div>
      )}
    </form>
  )
}
