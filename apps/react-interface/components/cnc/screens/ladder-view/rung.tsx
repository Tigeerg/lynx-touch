import { Fragment, useEffect, useRef, type ReactNode } from 'react'
import {
  coilKind,
  compareText,
  DEFAULT_PRESET_COUNT,
  DEFAULT_PRESET_MS,
  isCounterKind,
  isTimerKind,
  LADDER_MAX_CONTACTS,
  rungBranches,
} from '@/lib/cnc/ladder'
import type { LadderRungResult } from '@/lib/cnc/ladder-scan'
import type { LadderCoil, LadderContact, LadderRung } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

export function Wire({ on }: { on: boolean }) {
  return <span aria-hidden="true" className={cn('h-0.5 min-w-1 flex-1', on ? 'bg-success' : 'bg-foreground/25')} />
}

export function Slot({
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

export function Contact({
  c,
  inOn,
  on,
  selected,
}: {
  c: LadderContact
  inOn: boolean
  on: boolean
  selected: boolean
}) {
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

export function Coil({ c, result, selected }: { c: LadderCoil; result: LadderRungResult; selected: boolean }) {
  const kind = coilKind(c)
  const { on, out } = result

  if (isTimerKind(kind) || isCounterKind(kind)) {
    const counter = isCounterKind(kind)
    const preset = counter ? (c.presetCount ?? DEFAULT_PRESET_COUNT) : (c.presetMs ?? DEFAULT_PRESET_MS)
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

export function Rung({
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
