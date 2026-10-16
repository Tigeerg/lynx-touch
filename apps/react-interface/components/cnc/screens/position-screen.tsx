import { EyeOff, Move } from 'lucide-react'
import { distanceToGo, toWork, type Action } from '@/lib/cnc/machine'
import type { PathView } from '@/lib/cnc/toolpath'
import { AXES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { MdiInput } from '../mdi-input'
import { formatCoord, Keycap, Panel } from '../primitives'
import { ToolpathView } from '../toolpath-view'

export function PositionScreen({
  state,
  dispatch,
  pathView,
  showPath: pathEnabled,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  pathView: PathView
  showPath: boolean
}) {
  const mdi = state.mode === 'MDI'
  const pathVisible = pathEnabled && !mdi
  const showPath = pathVisible
  const work = toWork(state)
  const dtg = distanceToGo(state)
  const manual = state.mode === 'JOG' || state.mode === 'MPG'
  const offsetCode = state.offsets[state.activeOffset].code

  return (
    <div className="flex h-full min-h-0 flex-col gap-1.5">
      <Panel
        title="Положение осей"
        icon={Move}
        className={showPath ? 'shrink-0' : 'min-h-0 flex-1'}
        right={
          <div className="flex items-center gap-3">
            {!pathVisible && !mdi && (
              <span className="flex items-center gap-1.5 text-xs font-medium text-muted-foreground">
                <EyeOff className="size-3.5" aria-hidden="true" />
                Траектория скрыта <Keycap className="h-5 text-[10px]">P</Keycap>
              </span>
            )}
            <span className="font-mono text-xs font-semibold text-foreground">{offsetCode} · мм</span>
          </div>
        }
      >
        <table className={cn('w-full table-fixed border-collapse', !showPath && 'h-full')}>
          <thead>
            <tr className="border-b text-left text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
              <th className="w-16 px-2 py-1">Ось</th>
              <th className="px-2 py-1">Рабочие ({offsetCode})</th>
              <th className="w-[17%] px-2 py-1 text-right">Машинные</th>
              <th className="w-[17%] px-2 py-1 text-right">Остаток</th>
              <th className="w-[15%] px-2 py-1 text-right">Отн.</th>
            </tr>
          </thead>
          <tbody>
            {AXES.map((ax) => {
              const selected = manual && state.jogAxis === ax
              const rel = state.machine[ax] - state.relOrigin[ax]
              return (
                <tr
                  key={ax}
                  aria-selected={selected}
                  className={cn('border-b last:border-b-0', selected && 'bg-accent')}
                >
                  <td className="relative px-2 py-0.5">
                    {selected && <span className="absolute inset-y-0 left-0 w-1 bg-primary" aria-hidden="true" />}
                    <div className="flex items-center gap-1.5">
                      <span
                        className={cn(
                          'font-mono font-bold',
                          showPath ? 'text-xl' : 'text-3xl',
                          selected ? 'text-primary' : 'text-foreground',
                        )}
                      >
                        {ax}
                      </span>
                      <Keycap className={cn('h-5 min-w-5 px-1 text-[10px]', !manual && 'opacity-40')}>{ax}</Keycap>
                    </div>
                  </td>
                  <td
                    className={cn(
                      'px-2 py-0.5 font-mono font-bold tabular-nums tracking-tight',
                      showPath ? 'text-3xl' : 'text-5xl',
                    )}
                  >
                    {formatCoord(work[ax])}
                    {ax === 'A' && <span className="ml-1 font-sans text-sm text-muted-foreground">°</span>}
                  </td>
                  <td
                    className={cn(
                      'px-2 py-0.5 text-right font-mono tabular-nums text-muted-foreground',
                      showPath ? 'text-sm' : 'text-lg',
                    )}
                  >
                    {formatCoord(state.machine[ax])}
                  </td>
                  <td
                    className={cn(
                      'px-2 py-0.5 text-right font-mono font-semibold tabular-nums',
                      showPath ? 'text-sm' : 'text-lg',
                      Math.abs(dtg[ax]) > 0.0005 ? 'text-primary' : 'text-muted-foreground/60',
                    )}
                  >
                    {formatCoord(dtg[ax])}
                  </td>
                  <td
                    className={cn(
                      'px-2 py-0.5 text-right font-mono tabular-nums text-muted-foreground',
                      showPath ? 'text-sm' : 'text-lg',
                    )}
                  >
                    {formatCoord(rel)}
                  </td>
                </tr>
              )
            })}
          </tbody>
        </table>
      </Panel>

      {mdi ? (
        <MdiInput state={state} dispatch={dispatch} className="shrink-0" />
      ) : (
        pathVisible && <ToolpathView state={state} view={pathView} className="min-h-0 flex-1" />
      )}
    </div>
  )
}
