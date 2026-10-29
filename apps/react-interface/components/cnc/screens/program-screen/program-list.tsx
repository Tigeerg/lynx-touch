import { useEffect, useRef, type Dispatch } from 'react'
import { ArrowDown, ArrowUp, ChevronsDown, ChevronsUp, FileCode, Play, SquarePen } from 'lucide-react'
import { currentFile, type Action } from '@/lib/cnc/machine'
import { RAPID_RATE } from '@/lib/cnc/data'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { remainingTimeMs } from '@/lib/cnc/toolpath'
import { formatDuration, Lamp, Panel } from '../../primitives'
import { useUiMode } from '../../ui-mode-context'
import { highlight } from './highlight'
import { LineEditor } from './line-editor'

const RUN_LABEL: Record<MachineState['run'], string> = {
  IDLE: 'Ожидание пуска',
  RUN: 'Выполнение',
  HOLD: 'Подача остановлена',
  ALARM: 'Авария',
  ESTOP: 'Аварийный стоп',
}

// Progress strip of the running program: run state, bar, current block and per cent.
export function AutoProgress({ state }: { state: MachineState }) {
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
