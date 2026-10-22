// Common machine-state checks: busy, lock and the shared skip for motion actions. Extracted
// from machine.ts (tools/check.ps1 mark).
import { notify } from './messages'
import type { MachineState } from './types'

export const MACHINE_LOCK_MSG = 'Включена блокировка станка — оси не перемещаются'

export const isBusy = (s: MachineState) => s.run === 'RUN' || s.run === 'HOLD'

// Shared skip for motion actions ("safe retract", "go to part zero"): busy and machine lock
// are checked the same way in every path, so one of them does not forget the condition and
// does not move the axes when it is forbidden.
export const blockedFromMotion = (s: MachineState, busyMessage: string): MachineState | null => {
  if (isBusy(s)) return notify(s, busyMessage, 'warn')
  if (s.machineLock) return notify(s, MACHINE_LOCK_MSG, 'warn')
  return null
}
