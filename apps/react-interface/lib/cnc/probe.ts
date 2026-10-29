import type { Axes, Axis } from './types'

export const PROBE_TOOL = 99
export const PROBE_BALL_R = 1
export const PROBE_FEED = 100
export const PROBE_FAST_FEED = 500
export const PROBE_CLEARANCE = 5
export const TOUCH_MS = 1400

export interface P3 {
  x: number
  y: number
  z: number
}

export interface Touch {
  axis: Axis
  sign: 1 | -1
  from: P3
  to: P3
}

export interface ProbeView {
  plane: 'XY' | 'XZ'
  ox: number
  oy: number
  k: number
  stock: [number, number, number, number]
  hole?: { cx: number; cy: number; r: number }
  feature: 'edge-xp' | 'edge-xn' | 'edge-yp' | 'edge-yn' | 'top' | 'corner' | 'center'
}

export type ProbeGroup = 'edge' | 'surface' | 'feature'

export const PROBE_GROUPS: { id: ProbeGroup; label: string }[] = [
  { id: 'edge', label: 'Кромки' },
  { id: 'surface', label: 'Плоскость' },
  { id: 'feature', label: 'Углы и центры' },
]

export interface ProbeResultLine {
  label: string
  value: string
}

export interface ProbeCycle {
  id: string
  group: ProbeGroup
  name: string
  short: string
  axes: Axis[]
  start: P3
  startHint: string
  view: ProbeView
  touches: Touch[]
  compute: (c: P3[]) => { feature: Partial<P3>; info: ProbeResultLine[] }
}

// The cycle table and its geometry live in probe-shapes.ts (pure data, built on the types above);
// re-exported so probe.ts keeps one public surface for the run, the nav and the wizard.
export { PROBE_CYCLES } from './probe-shapes'

export const PROBE_STEPS = ['Цикл', 'Щуп', 'Подвод', 'Измерение', 'Результат'] as const
export const PROBE_STEP_RESULT = PROBE_STEPS.length - 1
export const PROBE_STEP_MEASURE = 3

export interface ProbeResult {
  values: Partial<Axes>
  info: ProbeResultLine[]
}

export const lerp3 = (a: P3, b: P3, t: number): P3 => ({
  x: a.x + (b.x - a.x) * t,
  y: a.y + (b.y - a.y) * t,
  z: a.z + (b.z - a.z) * t,
})

/** Probe tip position in cycle-local mm for a given elapsed time of the measuring run. */
export function probeLocalPosition(cycle: ProbeCycle, elapsed: number): { pos: P3; touch: number; touching: boolean } {
  const n = cycle.touches.length
  const idx = Math.min(n - 1, Math.floor(elapsed / TOUCH_MS))
  if (elapsed >= n * TOUCH_MS) return { pos: cycle.start, touch: n, touching: false }
  const t = (elapsed - idx * TOUCH_MS) / TOUCH_MS
  const touch = cycle.touches[idx]
  const prev = idx === 0 ? cycle.start : cycle.touches[idx - 1].from
  if (t < 0.3) return { pos: lerp3(prev, touch.from, t / 0.3), touch: idx, touching: false }
  if (t < 0.75) return { pos: lerp3(touch.from, touch.to, (t - 0.3) / 0.45), touch: idx, touching: false }
  return { pos: lerp3(touch.to, touch.from, (t - 0.75) / 0.25), touch: idx, touching: t < 0.8 }
}

export const localToMachine = (cycle: ProbeCycle, origin: Axes, l: P3): Axes => ({
  X: origin.X + l.x - cycle.start.x,
  Y: origin.Y + l.y - cycle.start.y,
  Z: origin.Z + l.z - cycle.start.z,
  A: origin.A,
})

export function computeProbeResult(cycle: ProbeCycle, origin: Axes): ProbeResult {
  const contacts = cycle.touches.map((t) => t.to)
  const { feature, info } = cycle.compute(contacts)
  const m = localToMachine(cycle, origin, { x: feature.x ?? 0, y: feature.y ?? 0, z: feature.z ?? 0 })
  const values: Partial<Axes> = {}
  if (feature.x !== undefined) values.X = Math.round(m.X * 1000) / 1000
  if (feature.y !== undefined) values.Y = Math.round(m.Y * 1000) / 1000
  if (feature.z !== undefined) values.Z = Math.round(m.Z * 1000) / 1000
  return { values, info }
}
