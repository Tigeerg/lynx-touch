// Координатные функции станка: рабочее смещение, перевод в рабочие координаты,
// остаток пути и скорость текущего перемещения. Вынесены из machine.ts, чтобы
// редуктор сводил подсистемы и не рос (метка tools/check.ps1); компоненты берут их
// из machine.ts (реэкспорт).
import { DRY_RUN_RATE, RAPID_RATE } from './data'
import type { Axes, MachineState } from './types'

export const zeroAxes = (): Axes => ({ X: 0, Y: 0, Z: 0, A: 0 })

export const workOffset = (s: MachineState) => s.offsets[s.activeOffset].values

export const toWork = (s: MachineState): Axes => {
  const off = workOffset(s)
  return { X: s.machine.X - off.X, Y: s.machine.Y - off.Y, Z: s.machine.Z - off.Z, A: s.machine.A - off.A }
}

export const distanceToGo = (s: MachineState): Axes => {
  if (!s.motion) return zeroAxes()
  const t = s.motion.target
  return { X: t.X - s.machine.X, Y: t.Y - s.machine.Y, Z: t.Z - s.machine.Z, A: t.A - s.machine.A }
}

export const motionSpeed = (s: MachineState): number => {
  if (!s.motion) return 0
  if (s.motion.rapid) return (RAPID_RATE * s.rapidOverride) / 100
  if (s.dryRun) return DRY_RUN_RATE
  return (s.motion.feed * s.feedOverride) / 100
}
