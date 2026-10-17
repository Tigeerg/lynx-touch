// Общие проверки состояния станка: занятость, блокировка и общий пропуск для
// действий перемещения. Вынесено из machine.ts (метка tools/check.ps1).
import { notify } from './messages'
import type { MachineState } from './types'

export const MACHINE_LOCK_MSG = 'Включена блокировка станка — оси не перемещаются'

export const isBusy = (s: MachineState) => s.run === 'RUN' || s.run === 'HOLD'

// Общий пропуск для действий перемещения («безопасный отвод», «в нуль детали»):
// занятость и блокировка станка проверяются одинаково во всех путях, чтобы один
// из них не забыл условие и не двинул оси, когда это запрещено.
export const blockedFromMotion = (s: MachineState, busyMessage: string): MachineState | null => {
  if (isBusy(s)) return notify(s, busyMessage, 'warn')
  if (s.machineLock) return notify(s, MACHINE_LOCK_MSG, 'warn')
  return null
}
