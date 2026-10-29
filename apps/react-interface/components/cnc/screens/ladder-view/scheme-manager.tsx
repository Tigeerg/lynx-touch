import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { Keycap } from '../../primitives'
import { FormPanel } from './form-panel'

type Dispatch = (a: Action) => void

export function SchemeManager({ state, dispatch }: { state: MachineState; dispatch: Dispatch }) {
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
