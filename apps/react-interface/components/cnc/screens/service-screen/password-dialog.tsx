import { KeyRound } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import { SERVICE_PASSWORD } from '@/lib/cnc/service-data'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

/** The Windows IME processing key code: a keydown carrying it is composition, not a command. */
const IME_KEYCODE = 229

export function PasswordDialog({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const auth = state.service.auth
  if (!auth) return null
  return (
    <div className="absolute inset-0 z-20 flex items-center justify-center bg-background/80 backdrop-blur-[1px]">
      <form
        role="dialog"
        aria-label="Доступ к записи параметров"
        onSubmit={(e) => e.preventDefault()}
        className="flex w-80 flex-col gap-3 rounded-md border-2 border-primary bg-card p-4 shadow-lg"
      >
        <div className="flex items-center gap-2">
          <KeyRound className="size-5 text-primary" aria-hidden="true" />
          <h3 className="text-base font-bold">Доступ к записи</h3>
        </div>
        <label className="flex flex-col gap-1 text-xs font-semibold uppercase tracking-wider text-muted-foreground">
          Пароль
          <input
            type="password"
            autoFocus
            autoComplete="off"
            value={auth.input}
            aria-invalid={auth.error}
            onChange={(e) => dispatch({ type: 'SERVICE_AUTH_INPUT', value: e.target.value })}
            onKeyDown={(e) => {
              if (e.nativeEvent.isComposing || e.nativeEvent.keyCode === IME_KEYCODE) return
              if (e.key === 'Enter') {
                e.preventDefault()
                dispatch({ type: 'SERVICE_ENTER' })
              } else if (e.key === 'Escape') {
                e.preventDefault()
                dispatch({ type: 'SERVICE_CANCEL' })
              }
            }}
            className={cn(
              'h-10 rounded-sm border bg-background px-3 font-mono text-lg tracking-widest text-foreground outline-none focus-visible:ring-2 focus-visible:ring-primary',
              auth.error && 'border-destructive',
            )}
          />
        </label>
        <p
          role="status"
          className={cn('min-h-4 text-xs', auth.error ? 'font-semibold text-destructive' : 'text-muted-foreground')}
        >
          {auth.error ? 'Неверный пароль' : `Демо-пароль: ${SERVICE_PASSWORD}`}
        </p>
        <div className="flex gap-2">
          <button
            type="button"
            onClick={() => dispatch({ type: 'SERVICE_ENTER' })}
            className="h-9 flex-1 rounded-sm bg-primary text-sm font-bold text-primary-foreground"
          >
            Ввод
          </button>
          <button
            type="button"
            onClick={() => dispatch({ type: 'SERVICE_CANCEL' })}
            className="h-9 flex-1 rounded-sm border bg-muted text-sm font-bold"
          >
            Отмена
          </button>
        </div>
      </form>
    </div>
  )
}
