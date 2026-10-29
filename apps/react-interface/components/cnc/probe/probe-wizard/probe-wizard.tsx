import { LocateFixed } from 'lucide-react'
import { PROBE_CYCLES, PROBE_STEP_RESULT } from '@/lib/cnc/probe'
import type { MachineState } from '@/lib/cnc/types'
import { Keycap, Panel } from '../../primitives'
import { ProbeSchematic } from '../probe-schematic'
import { CycleList, ProbeBadge } from './cycle-list'
import { Readout } from './readout'
import { Stepper } from './stepper'
import { StepCard } from './step-card'

// The probe wizard: step bar on top, cycle picker, scheme with the live readout, and the step card.
export function ProbeWizard({ state }: { state: MachineState }) {
  const p = state.probe
  const cycle = PROBE_CYCLES[p.cycle]
  const target = state.offsets[state.cursor.offset]

  return (
    <Panel
      title="3D щуп — замер заготовки"
      icon={LocateFixed}
      className="h-full"
      right={
        <span className="flex items-center gap-2 text-xs font-medium text-muted-foreground">
          Запись в
          <span className="rounded-sm bg-primary px-2 py-0.5 font-mono text-sm font-bold text-primary-foreground">
            {target.code}
          </span>
          {p.step > 0 && <Keycap className="h-5 text-[10px]">↑ ↓</Keycap>}
        </span>
      }
    >
      <div className="flex h-full min-h-0 flex-col">
        <Stepper step={p.step} />
        <div className="grid min-h-0 flex-1 grid-cols-[216px_minmax(0,1fr)_300px]">
          <CycleList selected={p.cycle} locked={p.step > 0} />
          <div className="flex min-h-0 flex-col border-x bg-background">
            <div className="flex min-h-0 flex-1">
              <div className="relative min-h-0 min-w-0 flex-1 p-2">
                <div className="h-full overflow-hidden rounded-md border bg-card">
                  <ProbeSchematic
                    cycle={cycle}
                    elapsed={p.elapsed}
                    running={p.running}
                    done={p.step === PROBE_STEP_RESULT}
                  />
                </div>
                <ProbeBadge state={state} />
              </div>
              <Readout state={state} />
            </div>
          </div>
          <StepCard state={state} cycle={cycle} />
        </div>
      </div>
    </Panel>
  )
}
