// Правка сервисных параметров: отмена изменения, отметка сохранения и установка
// значения с пересчётом пределов корректоров. Вынесено из machine.ts, чтобы
// редуктор сводил подсистемы и не рос (метка tools/check.ps1).
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM, type ParamDef } from './service-data'
import type { MachineState } from './types'

export const cancelEdit = (s: MachineState): MachineState => {
  const editing = s.service.editing
  if (!editing) return s
  return { ...s, params: { ...s.params, [editing.id]: editing.original }, service: { ...s.service, editing: null } }
}

export const markSaved = (s: MachineState, id: string, value: number): MachineState => ({
  ...s,
  service: { ...s.service, saved: { ...s.service.saved, [id]: value } },
})

export function setParam(s: MachineState, def: ParamDef, value: number): MachineState {
  const n: MachineState = {
    ...s,
    params: { ...s.params, [def.id]: value },
    service: { ...s.service, restartPending: s.service.restartPending || !!def.restart },
  }
  if (def.id === MAX_FEED_PARAM) n.feedOverride = Math.min(n.feedOverride, value)
  if (def.id === MAX_SPINDLE_PARAM) n.spindleOverride = Math.min(n.spindleOverride, value)
  return n
}
