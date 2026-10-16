'use client'

import { ArrowDown, ArrowUp, CornerDownLeft, FileCode, FolderOpen } from 'lucide-react'
import type { Dispatch } from 'react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Overlay } from './overlay'
import { Keycap } from './primitives'

export function FileDialog({ state, dispatch }: { state: MachineState; dispatch: Dispatch<Action> }) {
  return (
    <Overlay
      onClose={() => dispatch({ type: 'FILE_DIALOG', open: false })}
      labelledBy="file-dialog-title"
      className="flex w-full max-w-3xl flex-col border-2 border-foreground/20"
    >
      <header className="flex items-center gap-2 border-b bg-muted px-4 py-3">
        <FolderOpen className="size-5 text-primary" aria-hidden="true" />
        <h2 id="file-dialog-title" className="text-sm font-bold uppercase tracking-wide">
          Открыть файл программы
        </h2>
        <span className="ml-auto font-mono text-xs text-muted-foreground">//CNC_MEM/PROG</span>
      </header>

      <div className="grid grid-cols-[6rem_1fr_6rem_9rem] border-b px-4 py-2 text-xs font-semibold uppercase text-muted-foreground">
        <span>Файл</span>
        <span>Описание</span>
        <span className="text-right">Кадров</span>
        <span className="text-right">Изменён</span>
      </div>

      <ul aria-label="Файлы программ" className="divide-y">
        {state.files.map((f, i) => {
          const selected = i === state.fileCursor
          const active = i === state.activeFile
          return (
            <li key={f.name}>
              <button
                type="button"
                tabIndex={-1}
                aria-current={selected ? 'true' : undefined}
                onClick={() => dispatch({ type: 'FILE_CURSOR', delta: i - state.fileCursor })}
                onDoubleClick={() => dispatch({ type: 'FILE_OPEN' })}
                className={cn(
                  'grid w-full grid-cols-[6rem_1fr_6rem_9rem] items-center px-4 py-2.5 text-left',
                  selected ? 'bg-primary text-primary-foreground' : 'hover:bg-muted',
                )}
              >
                <span className="flex items-center gap-2 font-mono font-bold">
                  <FileCode className="size-4 shrink-0" aria-hidden="true" />
                  {f.name}
                </span>
                <span className="flex items-center gap-2 truncate font-medium">
                  {f.title}
                  {active && (
                    <span
                      className={cn(
                        'rounded-sm px-1.5 text-[10px] font-bold uppercase',
                        selected ? 'bg-primary-foreground text-primary' : 'bg-accent text-accent-foreground',
                      )}
                    >
                      Загружен
                    </span>
                  )}
                </span>
                <span className="text-right font-mono tabular-nums">{f.lines.length}</span>
                <span className={cn('text-right font-mono text-sm tabular-nums', !selected && 'text-muted-foreground')}>
                  {f.modified}
                </span>
              </button>
            </li>
          )
        })}
      </ul>

      <footer className="flex items-center gap-4 border-t bg-muted px-4 py-2.5 text-xs text-muted-foreground">
        <span className="flex items-center gap-1">
          <Keycap>
            <ArrowUp className="size-3" aria-label="Вверх" />
          </Keycap>
          <Keycap>
            <ArrowDown className="size-3" aria-label="Вниз" />
          </Keycap>
          выбор
        </span>
        <span className="flex items-center gap-1">
          <Keycap>
            <CornerDownLeft className="size-3" aria-label="Ввод" />
          </Keycap>
          открыть
        </span>
        <span className="flex items-center gap-1">
          <Keycap>Esc</Keycap>
          закрыть
        </span>
      </footer>
    </Overlay>
  )
}
