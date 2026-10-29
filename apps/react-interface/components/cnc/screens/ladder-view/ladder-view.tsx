import { useMemo } from 'react'
import {
  activeScheme,
  coilKind,
  isCounterKind,
  isTimerKind,
  LADDER_MAX_CONTACTS,
  LADDER_SIGNALS,
  ladderCursor,
  rungBranches,
} from '@/lib/cnc/ladder'
import type { Action } from '@/lib/cnc/machine'
import { evaluateLadder } from '@/lib/cnc/ladder-scan'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap } from '../../primitives'
import { FormPanel } from './form-panel'
import { Rung } from './rung'
import { SchemeManager } from './scheme-manager'

type Dispatch = (a: Action) => void

// The reference addresses are constant: building the set on every render was pure waste.
const KNOWN_SIGNAL_ADDRS = new Set(LADDER_SIGNALS.map((x) => x.addr))

export function LadderView({ state, dispatch }: { state: MachineState; dispatch: Dispatch }) {
  const L = state.ladder
  const scheme = activeScheme(state)
  const editing = L.draft !== null
  const { rungs, row, path, contacts, col, onCoil } = ladderCursor(state)
  const result = evaluateLadder(state, rungs)

  const coilNow = rungs[row]?.coil
  const kindNow = onCoil && coilNow ? coilKind(coilNow) : null
  const timerCoil = !!kindNow && isTimerKind(kindNow)
  const counterCoil = !!kindNow && isCounterKind(kindNow)
  const preset: 'timer' | 'counter' | null = timerCoil ? 'timer' : counterCoil ? 'counter' : null
  // The chip list is filtered out of the rungs on every render otherwise; the rung array is stable
  // until the draft changes.
  const { chips } = useMemo(() => {
    const userCoils = rungs
      .filter((r) => !KNOWN_SIGNAL_ADDRS.has(r.coil.addr))
      .map((r) => r.coil)
      .filter(
        (c) =>
          !onCoil ||
          (timerCoil
            ? isTimerKind(coilKind(c))
            : counterCoil
              ? isCounterKind(coilKind(c))
              : !isTimerKind(coilKind(c)) && !isCounterKind(coilKind(c))),
      )
    const base = timerCoil
      ? LADDER_SIGNALS.filter((x) => x.kind === 'timer')
      : counterCoil
        ? []
        : LADDER_SIGNALS.filter((x) => !onCoil || x.kind === 'out' || x.kind === 'flag')
    return { chips: [...base, ...userCoils.map((c) => ({ addr: c.addr, label: c.label }))] }
  }, [rungs, onCoil, timerCoil, counterCoil])

  return (
    <div className="flex min-h-0 flex-1 flex-col">
      <div className="flex shrink-0 items-center gap-3 border-b px-3 py-1.5 text-xs">
        <span className="font-mono text-sm font-bold">{scheme.name}.lad</span>
        <span className="min-w-0 truncate text-muted-foreground">{scheme.title}</span>
        {L.manager ? (
          <span className="rounded-sm bg-primary px-1.5 py-0.5 text-[10px] font-bold text-primary-foreground">
            СХЕМЫ
          </span>
        ) : editing ? (
          <span
            className={cn(
              'rounded-sm px-1.5 py-0.5 text-[10px] font-bold',
              L.dirty ? 'bg-warning text-warning-foreground' : 'bg-primary text-primary-foreground',
            )}
          >
            {L.dirty ? 'ПРАВКА · ИЗМЕНЕНО' : 'ПРАВКА'}
          </span>
        ) : (
          <span className="rounded-sm bg-success px-1.5 py-0.5 text-[10px] font-bold text-primary-foreground">
            ОНЛАЙН
          </span>
        )}
        <span className="ml-auto shrink-0 font-mono text-muted-foreground">
          {rungs.length} ступ. · {scheme.modified}
        </span>
      </div>

      {L.manager ? (
        <SchemeManager state={state} dispatch={dispatch} />
      ) : (
        <>
          <ol aria-label="Ступени схемы" className="flex min-h-0 flex-1 flex-col overflow-y-auto px-3 py-1">
            {rungs.map((rung, i) => (
              <Rung
                key={i}
                index={i}
                rung={rung}
                result={result.rungs[i]}
                rowSelected={i === row}
                path={path}
                col={col}
                editing={editing}
              />
            ))}
          </ol>

          {L.form?.kind === 'element' ? (
            <FormPanel
              form={L.form}
              title={onCoil ? 'Катушка' : contacts[col]?.cmp ? 'Сравнение' : 'Контакт'}
              dispatch={dispatch}
              chips={contacts[col]?.cmp ? undefined : chips}
              preset={preset}
            />
          ) : editing ? (
            <p className="flex shrink-0 items-center gap-2 border-t px-3 py-1.5 text-xs text-muted-foreground">
              <Keycap>↑↓</Keycap> ступень <Keycap>Shift+↑↓</Keycap> ветвь <Keycap>←→</Keycap> элемент{' '}
              <Keycap>Enter</Keycap> свойства
              <Keycap>Ins</Keycap> контакт <Keycap>Del</Keycap> удалить <Keycap>Esc</Keycap> отмена
              <span className="ml-auto">
                ветвь {path + 1}/{rungBranches(rungs[row]).length} · {contacts.length}/{LADDER_MAX_CONTACTS}
              </span>
            </p>
          ) : (
            <p className="flex shrink-0 items-center gap-4 border-t px-3 py-1.5 text-xs text-muted-foreground">
              <span className="font-semibold">ClassicLadder · онлайн-мониторинг</span>
              <span className="flex items-center gap-1.5">
                <span className="h-0.5 w-5 bg-success" aria-hidden="true" /> есть ток
              </span>
              <span className="flex items-center gap-1.5">
                <span className="h-0.5 w-5 bg-foreground/25" aria-hidden="true" /> нет тока
              </span>
            </p>
          )}
        </>
      )}
    </div>
  )
}
