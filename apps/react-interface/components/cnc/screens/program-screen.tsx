'use client'

import { ArrowDown, ArrowUp, ChevronsDown, ChevronsUp, FileCode, Play, Search, SquarePen, X } from 'lucide-react'
import { useEffect, useMemo, useRef, type Dispatch } from 'react'
import { currentFile, distanceToGo, programLines, toWork, type Action } from '@/lib/cnc/machine'
import { RAPID_RATE } from '@/lib/cnc/data'
import { AXES, type Axis, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { buildToolpath, remainingTimeMs, simPose, type PathView } from '@/lib/cnc/toolpath'
import { formatCoord, formatDuration, Lamp, Panel } from '../primitives'
import { ToolpathView } from '../toolpath-view'
import { useUiMode } from '../ui-mode-context'

function highlight(line: string) {
  const comment = line.match(/\(.*\)/)?.[0]
  const code = comment ? line.replace(comment, '') : line
  const parts = code.split(/(\s+)/).map((word, i) => {
    const letter = word[0]
    const tone =
      letter === 'N'
        ? 'text-muted-foreground'
        : letter === 'G'
          ? 'text-primary font-bold'
          : letter === 'M'
            ? 'text-destructive font-bold'
            : letter === 'T' || letter === 'S' || letter === 'F'
              ? 'text-success font-semibold'
              : ''
    return (
      <span key={i} className={tone}>
        {word}
      </span>
    )
  })
  return (
    <>
      {parts}
      {comment && <span className="italic text-muted-foreground">{comment}</span>}
    </>
  )
}

function LineEditor({ value, dispatch }: { value: string; dispatch: Dispatch<Action> }) {
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

export function ProgramScreen({
  state,
  dispatch,
  pathView,
  showPath,
}: {
  state: MachineState
  dispatch: Dispatch<Action>
  pathView: PathView
  showPath: boolean
}) {
  const auto = state.mode === 'AUTO'

  return (
    <div className="flex h-full min-h-0 flex-col gap-1.5">
      {auto && <AutoProgress state={state} />}
      <DroStrip state={state} />
      {state.search.open && <SearchBar state={state} dispatch={dispatch} />}
      <div
        className={cn(
          'grid min-h-0 flex-1 gap-1.5',
          !showPath
            ? 'grid-cols-1'
            : auto
              ? 'grid-cols-[minmax(0,1fr)_minmax(0,1.15fr)]'
              : 'grid-cols-[minmax(0,1fr)_minmax(0,0.8fr)]',
        )}
      >
        <ProgramList state={state} dispatch={dispatch} />
        {showPath && <ToolpathView state={state} view={pathView} className="h-full" />}
      </div>
    </div>
  )
}

const droCol = 'grid grid-cols-[3.4rem_repeat(4,minmax(0,1fr))] items-baseline gap-x-2'

function DroRow({ label, values }: { label: string; values: (number | null)[] }) {
  return (
    <>
      <span className="font-sans text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
        {label}
      </span>
      {values.map((v, i) => (
        <span key={i} className="text-right font-mono text-sm font-bold leading-tight tabular-nums">
          {v === null ? <span className="font-normal text-muted-foreground">—</span> : formatCoord(v)}
        </span>
      ))}
    </>
  )
}

function DroStrip({ state }: { state: MachineState }) {
  const sim = state.sim.active
  const lines = programLines(state)
  const segments = useMemo(() => (sim ? buildToolpath(lines) : []), [sim, lines])
  const pose = useMemo(() => (sim ? simPose(segments, state.sim.line) : null), [sim, segments, state.sim.line])

  const xyz: ('X' | 'Y' | 'Z')[] = ['X', 'Y', 'Z']
  const mtn: (number | null)[] = AXES.map((ax: Axis) => state.machine[ax])
  const work: (number | null)[] = sim
    ? [...(pose ? xyz.map((ax) => pose.pos[ax]) : xyz.map(() => null)), null]
    : AXES.map((ax: Axis) => toWork(state)[ax])
  const dtg: (number | null)[] = sim
    ? [...(pose?.dtg ? xyz.map((ax) => pose.dtg![ax]) : xyz.map(() => null)), null]
    : AXES.map((ax: Axis) => distanceToGo(state)[ax])

  return (
    <div className="shrink-0 rounded-md border bg-card px-3 py-1">
      <div className={droCol}>
        <span className="font-sans text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">Ось</span>
        {AXES.map((ax) => (
          <span key={ax} className="text-right font-mono text-[10px] font-bold uppercase text-muted-foreground">
            {ax}
          </span>
        ))}
        <DroRow label="Машина" values={mtn} />
        <DroRow label="Рабочая" values={work} />
        <DroRow label="Остаток" values={dtg} />
      </div>
      {sim && (
        <p className="mt-0.5 flex items-center gap-1.5 text-[10px] font-bold uppercase tracking-wider text-warning">
          <Lamp on tone="warning" />
          Симуляция: оси неподвижны, кадр {state.sim.line + 1}/{lines.length}
        </p>
      )}
    </div>
  )
}

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

const RUN_LABEL: Record<MachineState['run'], string> = {
  IDLE: 'Ожидание пуска',
  RUN: 'Выполнение',
  HOLD: 'Подача остановлена',
  ALARM: 'Авария',
  ESTOP: 'Аварийный стоп',
}

function AutoProgress({ state }: { state: MachineState }) {
  const total = currentFile(state).lines.length
  const progress = Math.round((state.line / Math.max(1, total - 1)) * 100)
  const tone =
    state.run === 'RUN'
      ? 'bg-success'
      : state.run === 'HOLD'
        ? 'bg-warning'
        : state.run === 'IDLE'
          ? 'bg-primary'
          : 'bg-destructive'

  return (
    <div className="flex shrink-0 items-center gap-4 rounded-md border bg-card px-3 py-2">
      <div className="flex w-40 shrink-0 flex-col">
        <span className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
          Выполнение детали
        </span>
        <span className="truncate text-sm font-bold">{RUN_LABEL[state.run]}</span>
      </div>
      <div
        role="progressbar"
        aria-label="Прогресс выполнения программы"
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={progress}
        className="h-5 min-w-0 flex-1 overflow-hidden rounded-sm bg-muted"
      >
        <div className={cn('h-full transition-[width]', tone)} style={{ width: `${progress}%` }} />
      </div>
      <span className="shrink-0 font-mono text-xs text-muted-foreground tabular-nums">
        {state.line + 1}/{total}
      </span>
      <span className="w-20 shrink-0 text-right font-mono text-3xl font-bold leading-none tabular-nums">
        {progress}%
      </span>
    </div>
  )
}

export function ProgramList({
  state,
  dispatch,
  className,
  bare = false,
}: {
  state: MachineState
  dispatch: Dispatch<Action>
  className?: string
  /** Without its own frame and header: the block list for the common header. */
  bare?: boolean
}) {
  const listRef = useRef<HTMLOListElement>(null)
  // On the touch panel there are no physical arrow keys: a row is tapped to move the cursor and a
  // scroll column next to the list pages it. The classic panel keeps the rows read-only.
  const touch = useUiMode()?.mode === 'touch'
  const sim = state.sim.active
  const running = !sim && (state.run === 'RUN' || state.run === 'HOLD')
  const follow = sim ? state.sim.line : running ? state.line : state.cursor.prog
  const file = currentFile(state)
  const restMs = remainingTimeMs(
    file.lines,
    sim ? state.sim.line : state.line,
    state.programFeed || 1000,
    (RAPID_RATE * state.rapidOverride) / 100,
  )

  useEffect(() => {
    listRef.current?.querySelector<HTMLElement>(`[data-index="${follow}"]`)?.scrollIntoView({ block: 'nearest' })
  }, [follow, file.lines.length])

  const info = (
    <div className="flex items-center gap-2">
      <span className="font-mono text-[11px] font-semibold text-muted-foreground" title="Оценка остатка времени">
        ≈ {formatDuration(restMs)}
      </span>
      {state.editMode ? (
        <span className="flex items-center gap-1.5 rounded-sm bg-warning px-2 py-0.5 text-xs font-bold text-warning-foreground">
          <SquarePen className="size-3.5" aria-hidden="true" />
          РЕДАКТИРОВАНИЕ
        </span>
      ) : sim ? (
        <span className="flex items-center gap-1.5 rounded-sm bg-warning px-2 py-0.5 text-xs font-bold text-warning-foreground">
          <Play className="size-3.5 fill-current" aria-hidden="true" />
          СИМУЛЯЦИЯ
        </span>
      ) : (
        <div className="flex items-center gap-2 text-[11px] font-medium">
          <span className="flex items-center gap-1">
            <Lamp on={state.singleBlock} tone="warning" /> Покадр.
          </span>
          <span className="flex items-center gap-1">
            <Lamp on={state.optStop} tone="primary" /> M01
          </span>
          <span className="flex items-center gap-1">
            <Lamp on={state.dryRun} tone="warning" /> Холост.
          </span>
        </div>
      )}
    </div>
  )

  const list = (
    <div className="flex h-full min-h-0 gap-1.5">
      {touch && (
        <div className="flex w-12 shrink-0 flex-col gap-1.5" role="group" aria-label="Прокрутка кадров">
          {(
            [
              { label: 'Страница кадров вверх', icon: ChevronsUp, delta: -10 },
              { label: 'Кадр вверх', icon: ArrowUp, delta: -1 },
              { label: 'Кадр вниз', icon: ArrowDown, delta: 1 },
              { label: 'Страница кадров вниз', icon: ChevronsDown, delta: 10 },
            ] as const
          ).map(({ label, icon: Icon, delta }) => (
            <button
              key={label}
              type="button"
              tabIndex={-1}
              aria-label={label}
              onClick={() => dispatch({ type: 'CURSOR', delta })}
              className="flex h-11 w-11 shrink-0 touch-manipulation items-center justify-center rounded-md border border-b-[3px] bg-card font-semibold text-foreground transition-colors active:translate-y-px active:border-b active:bg-accent"
            >
              <Icon className="size-5" aria-hidden="true" />
            </button>
          ))}
        </div>
      )}
      <ol
        ref={listRef}
        className="h-full min-w-0 flex-1 overflow-hidden py-0.5 font-mono text-sm"
        aria-label="Кадры программы"
      >
        {file.lines.map((line, i) => {
          const current = !state.editMode && (sim ? i === state.sim.line : i === state.line)
          const cursor = i === state.cursor.prog
          const editing = state.editor?.line === i
          return (
            <li
              key={i}
              data-index={i}
              aria-current={current ? 'step' : undefined}
              role={touch ? 'button' : undefined}
              onClick={() => {
                if (!touch || state.editor || i === state.cursor.prog) return
                dispatch({ type: 'CURSOR', delta: i - state.cursor.prog })
              }}
              className={cn(
                'flex items-center gap-2 border-l-4 border-transparent px-2 py-0.5',
                touch && 'min-h-11 cursor-pointer touch-manipulation active:bg-accent',
                current && 'border-primary bg-accent',
                cursor && 'outline-2 -outline-offset-2 outline-foreground/40',
                cursor && touch && 'outline-primary',
                cursor && state.editMode && 'border-warning bg-warning/15',
              )}
            >
              <span className="w-8 shrink-0 text-right text-xs text-muted-foreground tabular-nums">{i + 1}</span>
              <span className="flex w-4 shrink-0 text-primary" aria-hidden="true">
                {current && <Play className="size-3.5 fill-current" />}
              </span>
              {editing ? (
                <LineEditor value={state.editor!.buffer} dispatch={dispatch} />
              ) : (
                <span className="truncate">{highlight(line)}</span>
              )}
            </li>
          )
        })}
      </ol>
    </div>
  )

  if (bare) return <div className={cn('h-full', className)}>{list}</div>

  return (
    <Panel
      title={`Программа ${file.name} · ${file.title}`}
      icon={FileCode}
      className={cn('h-full', className, state.editMode && 'border-warning')}
      right={info}
    >
      {list}
    </Panel>
  )
}
