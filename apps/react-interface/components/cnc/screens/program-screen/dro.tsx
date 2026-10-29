import { useMemo } from 'react'
import { distanceToGo, programLines, toWork } from '@/lib/cnc/machine'
import { AXES, type Axis, type MachineState } from '@/lib/cnc/types'
import { buildToolpath, simPose } from '@/lib/cnc/toolpath'
import { formatDro, Lamp } from '../../primitives'

const droCol = 'grid grid-cols-[3.4rem_repeat(4,minmax(0,1fr))] items-baseline gap-x-2'

function DroRow({ label, values }: { label: string; values: (number | null)[] }) {
  return (
    <>
      <span className="font-sans text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
        {label}
      </span>
      {values.map((v, i) => (
        <span key={i} className="text-right font-mono text-sm font-bold leading-tight tabular-nums">
          {v === null ? <span className="font-normal text-muted-foreground">—</span> : formatDro(v)}
        </span>
      ))}
    </>
  )
}

// Machine, work and distance-to-go readout; in simulation the axes stay put and the pose comes from the
// toolpath.
export function DroStrip({ state }: { state: MachineState }) {
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
