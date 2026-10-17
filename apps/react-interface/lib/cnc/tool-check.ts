import type { MachineState } from './types'

/** Допуск на расхождение длины инструмента при проверке слома, мм. */
export const BROKEN_TOL_MM = 0.3

/**
 * Длина, которую читает датчик обмера инструмента. Таблица инструмента выставлена
 * этим же датчиком, поэтому в норме расхождение — доли миллиметра: заметная усадка
 * или исчерпанный ресурс означают слом. Формула детерминирована: расхождение зависит
 * от положения Z, как у настоящего касания.
 */
export function sensorLength(s: MachineState, toolIndex: number): number {
  const stored = s.tools[toolIndex].length
  const deviation = ((Math.abs(s.machine.Z) * 7.31 + toolIndex * 13.7) % 0.2) - 0.1
  return Math.round((stored + deviation) * 1000) / 1000
}
