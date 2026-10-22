import type { MachineState } from './types'

/** Tolerance on a tool-length mismatch in the break check, mm. */
export const BROKEN_TOL_MM = 0.3

/**
 * The length the tool-setting sensor reads. The tool table is set with the same sensor, so
 * normally the mismatch is a fraction of a millimetre: noticeable shrinkage or an exhausted
 * life means a break. The formula is deterministic: the mismatch depends on the Z position,
 * as with a real touch.
 */
export function sensorLength(s: MachineState, toolIndex: number): number {
  const stored = s.tools[toolIndex].length
  const deviation = ((Math.abs(s.machine.Z) * 7.31 + toolIndex * 13.7) % 0.2) - 0.1
  return Math.round((stored + deviation) * 1000) / 1000
}
