import type { Dispatch } from 'react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import type { PathView } from '@/lib/cnc/toolpath'
import { ToolpathView } from '../../toolpath-view'
import { AutoProgress, ProgramList } from './program-list'
import { DroStrip } from './dro'
import { SearchBar } from './search-bar'

// The program screen: progress, DRO, search and the block list next to the toolpath.
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
