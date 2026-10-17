// Исполнение измерительного цикла щупа: подвод по локальной траектории цикла и
// запись результата. Вынесено из machine.ts (метка tools/check.ps1); сообщение
// показывает machine.ts, как и у других подсистем.
import {
  computeProbeResult,
  localToMachine,
  PROBE_CYCLES,
  PROBE_STEP_MEASURE,
  PROBE_STEP_RESULT,
  probeLocalPosition,
  TOUCH_MS,
} from './probe'
import type { MachineState } from './types'

export function probeTick(s: MachineState, dt: number): { s: MachineState; msg?: string } {
  const cycle = PROBE_CYCLES[s.probe.cycle]
  const origin = s.probe.origin ?? s.machine
  const elapsed = s.probe.elapsed + (dt * s.feedOverride) / 100
  if (elapsed >= cycle.touches.length * TOUCH_MS) {
    const result = computeProbeResult(cycle, origin)
    return {
      s: {
        ...s,
        run: 'IDLE',
        machine: { ...origin },
        probe: { ...s.probe, running: false, elapsed, result, step: PROBE_STEP_RESULT },
      },
      msg: `Измерение «${cycle.name}» завершено`,
    }
  }
  const { pos } = probeLocalPosition(cycle, elapsed)
  return { s: { ...s, machine: localToMachine(cycle, origin, pos), probe: { ...s.probe, elapsed } } }
}

export const abortProbe = (s: MachineState): MachineState =>
  s.probe.running
    ? {
        ...s,
        machine: s.probe.origin ? { ...s.probe.origin } : s.machine,
        probe: { ...s.probe, running: false, elapsed: 0, step: PROBE_STEP_MEASURE },
      }
    : s
