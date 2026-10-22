'use client'

import { Fragment, useEffect, useRef, type ReactNode } from 'react'
import {
  activeScheme,
  coilKind,
  compareText,
  DEFAULT_PRESET_COUNT,
  DEFAULT_PRESET_MS,
  isCounterKind,
  isTimerKind,
  LADDER_MAX_CONTACTS,
  LADDER_SIGNALS,
  ladderCursor,
  rungBranches,
} from '@/lib/cnc/ladder'
import type { Action } from '@/lib/cnc/machine'
import { evaluateLadder, type LadderRungResult } from '@/lib/cnc/ladder-scan'
import type { LadderCoil, LadderContact, LadderForm, LadderRung, MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap } from '../primitives'

type Dispatch = (a: Action) => void

function Wire({ on }: { on: boolean }) {
  return <span aria-hidden="true" className={cn('h-0.5 min-w-1 flex-1', on ? 'bg-success' : 'bg-foreground/25')} />
}

function Slot({
  children,
  top,
  bottom,
  selected,
}: {
  children: ReactNode
  top?: string
  bottom?: string
  selected?: boolean
}) {
  return (
    <div
      className={cn(
        'grid h-full min-w-0 grid-rows-[1rem_1.5rem_1rem] items-center rounded-sm',
        selected && 'bg-primary/15 outline-2 -outline-offset-2 outline-primary',
      )}
    >
      <span className="truncate px-1 text-center font-mono text-[11px] font-bold leading-4">{top}</span>
      <div className="flex h-6 items-center">{children}</div>
      <span className="truncate px-1 text-center text-[10px] leading-4 text-muted-foreground">{bottom}</span>
      {selected && <span className="sr-only">выбран</span>}
    </div>
  )
}

function Contact({ c, inOn, on, selected }: { c: LadderContact; inOn: boolean; on: boolean; selected: boolean }) {
  if (c.cmp) {
    return (
      <Slot top="CMP" bottom={c.label} selected={selected}>
        <Wire on={inOn} />
        <span
          aria-label={`Сравнение: ${compareText(c.cmp)}`}
          className={cn(
            'flex h-6 w-16 shrink-0 items-center justify-center border-y-2 border-dashed px-0.5 font-mono text-[9px] font-bold leading-none',
            on ? 'border-success text-success' : 'border-foreground/50 text-foreground/70',
          )}
        >
          {compareText(c.cmp)}
        </span>
        <Wire on={on} />
      </Slot>
    )
  }
  return (
    <Slot top={c.addr} bottom={c.label} selected={selected}>
      <Wire on={inOn} />
      <span
        className={cn(
          'flex h-6 w-5 shrink-0 items-center justify-center border-x-[3px]',
          on ? 'border-success bg-success/15 text-success' : 'border-foreground/50 text-foreground/60',
        )}
      >
        {c.nc && <span aria-hidden="true" className="h-0.5 w-6 -rotate-[60deg] bg-current" />}
      </span>
      <Wire on={on} />
    </Slot>
  )
}

const seconds = (ms: number) => (ms / 1000).toFixed(1).replace('.', ',')

function Coil({ c, result, selected }: { c: LadderCoil; result: LadderRungResult; selected: boolean }) {
  const kind = coilKind(c)
  const { on, out } = result

  if (isTimerKind(kind) || isCounterKind(kind)) {
    const counter = isCounterKind(kind)
    const preset = c.preset ?? (counter ? DEFAULT_PRESET_COUNT : DEFAULT_PRESET_MS)
    const cur = counter ? (result.count ?? 0) : (result.acc ?? 0)
    const fill = Math.min(100, preset > 0 ? (cur / preset) * 100 : 0)
    const bottom = counter ? `${cur} / ${preset}` : `${seconds(result.acc ?? 0)} / ${seconds(preset)} с`
    const name =
      kind === 'ton'
        ? 'Таймер задержки включения'
        : kind === 'tof'
          ? 'Таймер задержки выключения'
          : kind === 'ctu'
            ? 'Счётчик вверх'
            : 'Счётчик вниз'
    return (
      <Slot top={c.addr} bottom={bottom} selected={selected}>
        <Wire on={out} />
        <span
          role="img"
          aria-label={`${name}, выход ${on ? 'включён' : 'выключен'}`}
          className={cn(
            'relative flex h-6 w-14 shrink-0 items-center justify-center overflow-hidden border-2 font-mono text-[11px] font-bold',
            on ? 'border-success text-success' : 'border-foreground/50 text-foreground/70',
          )}
        >
          <span aria-hidden="true" className="absolute inset-y-0 left-0 bg-warning/30" style={{ width: `${fill}%` }} />
          <span className="relative">{kind.toUpperCase()}</span>
        </span>
        <Wire on={on} />
      </Slot>
    )
  }

  const mark = kind === 'set' ? 'S' : kind === 'reset' ? 'R' : null
  return (
    <Slot top={c.addr} bottom={c.label} selected={selected}>
      <Wire on={out} />
      <span
        aria-label={on ? 'Катушка включена' : 'Катушка выключена'}
        className={cn(
          'flex size-6 shrink-0 items-center justify-center rounded-full border-[3px] font-mono text-[11px] font-bold',
          on ? 'border-success bg-success text-primary-foreground' : 'border-foreground/50 text-foreground/70',
          mark && !on && 'bg-background',
        )}
      >
        {mark}
      </span>
      <Wire on={mark ? on : out} />
    </Slot>
  )
}

function Rung({
  index,
  rung,
  result,
  rowSelected,
  path,
  col,
  editing,
}: {
  index: number
  rung: LadderRung
  result: LadderRungResult
  rowSelected: boolean
  path: number
  col: number
  editing: boolean
}) {
  const ref = useRef<HTMLLIElement>(null)
  useEffect(() => {
    if (rowSelected) ref.current?.scrollIntoView({ block: 'nearest' })
  }, [rowSelected])

  const branches = rungBranches(rung)
  const coilCol = rung.contacts.length

  return (
    <li
      ref={ref}
      aria-selected={rowSelected}
      className={cn('flex shrink-0 items-center border-x-4 border-x-foreground/40', rowSelected && 'bg-accent')}
    >
      <span className="flex w-32 shrink-0 items-center px-2 text-xs font-semibold">
        <span className="truncate">
          <span className="font-mono text-muted-foreground">{String(index + 1).padStart(3, '0')}</span> {rung.title}
        </span>
      </span>
      <div className="flex min-w-0 flex-1 items-center border-x-2 border-foreground/40">
        <div
          className="grid min-w-0 flex-1 gap-y-1"
          style={{ gridTemplateColumns: `repeat(${LADDER_MAX_CONTACTS}, minmax(0, 1fr))` }}
        >
          {branches.map((contacts, bi) => {
            const bres = result.branches[bi] ?? { closed: [], on: false }
            let powered = true
            const cells = contacts.map((c, j) => {
              const inOn = powered
              powered = powered && bres.closed[j]
              return { c, inOn, on: powered }
            })
            const fillers = Math.max(0, LADDER_MAX_CONTACTS - cells.length)
            return (
              <Fragment key={bi}>
                {cells.map(({ c, inOn, on }, j) => (
                  <Contact
                    key={j}
                    c={c}
                    inOn={inOn}
                    on={on}
                    selected={editing && rowSelected && path === bi && col === j}
                  />
                ))}
                {Array.from({ length: fillers }, (_, k) => (
                  <Slot key={`f${bi}-${k}`}>
                    <Wire on={powered} />
                  </Slot>
                ))}
              </Fragment>
            )
          })}
        </div>
      </div>
      <div className="flex w-6 shrink-0 items-center">
        <Wire on={result.out} />
      </div>
      <div className="w-28 shrink-0">
        <Coil c={rung.coil} result={result} selected={editing && rowSelected && path === 0 && col === coilCol} />
      </div>
      <div className="flex w-4 shrink-0 items-center">
        <Wire on={result.on} />
      </div>
    </li>
  )
}

function Field({
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

function FormPanel({
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

function SchemeManager({ state, dispatch }: { state: MachineState; dispatch: Dispatch }) {
  const L = state.ladder
  return (
    <div className="flex min-h-0 flex-1 flex-col">
      <div className="min-h-0 flex-1 overflow-y-auto">
        <table className="w-full text-sm">
          <thead className="sticky top-0 bg-muted text-left text-[11px] uppercase text-muted-foreground">
            <tr>
              <th className="px-3 py-1.5 font-semibold">Файл</th>
              <th className="px-3 py-1.5 font-semibold">Описание</th>
              <th className="px-3 py-1.5 text-right font-semibold">Ступеней</th>
              <th className="px-3 py-1.5 font-semibold">Изменён</th>
              <th className="px-3 py-1.5 font-semibold">ПЛК</th>
            </tr>
          </thead>
          <tbody>
            {L.schemes.map((x, i) => (
              <tr
                key={x.name}
                aria-selected={i === L.pick}
                onClick={() => dispatch({ type: 'LADDER_PICK_MOVE', delta: i - L.pick })}
                className={cn(
                  'cursor-pointer border-b',
                  i === L.pick && 'bg-accent outline-2 -outline-offset-2 outline-primary',
                )}
              >
                <td className="px-3 py-1.5 font-mono font-bold">{x.name}.lad</td>
                <td className="truncate px-3 py-1.5">{x.title}</td>
                <td className="px-3 py-1.5 text-right font-mono">{x.rungs.length}</td>
                <td className="px-3 py-1.5 font-mono text-xs text-muted-foreground">{x.modified}</td>
                <td className="px-3 py-1.5">
                  {i === L.active && (
                    <span className="rounded-sm bg-success px-1.5 py-0.5 text-[10px] font-bold text-primary-foreground">
                      АКТИВНА
                    </span>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {L.form?.kind === 'scheme' ? (
        <FormPanel form={L.form} title="Схема" dispatch={dispatch} />
      ) : (
        <p className="flex shrink-0 items-center gap-2 border-t px-3 py-1.5 text-xs text-muted-foreground">
          <Keycap>↑↓</Keycap> выбор <Keycap>Enter</Keycap> загрузить в ПЛК <Keycap>Ins</Keycap> новая{' '}
          <Keycap>Del</Keycap> удалить
          <Keycap>Esc</Keycap> закрыть
          {L.dirty && <span className="ml-auto font-semibold text-warning">Есть несохранённые правки</span>}
        </p>
      )}
    </div>
  )
}

export function LadderView({ state, dispatch }: { state: MachineState; dispatch: Dispatch }) {
  const L = state.ladder
  const scheme = activeScheme(state)
  const editing = L.draft !== null
  const { rungs, row, path, contacts, col, onCoil } = ladderCursor(state)
  const result = evaluateLadder(state, rungs)

  const known = new Set(LADDER_SIGNALS.map((x) => x.addr))
  const coilNow = rungs[row]?.coil
  const kindNow = onCoil && coilNow ? coilKind(coilNow) : null
  const timerCoil = !!kindNow && isTimerKind(kindNow)
  const counterCoil = !!kindNow && isCounterKind(kindNow)
  const preset: 'timer' | 'counter' | null = timerCoil ? 'timer' : counterCoil ? 'counter' : null
  const userCoils = rungs
    .filter((r) => !known.has(r.coil.addr))
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
  const chips = [...base, ...userCoils.map((c) => ({ addr: c.addr, label: c.label }))]

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
