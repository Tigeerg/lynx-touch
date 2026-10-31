// The confirmation of the first start (PLAN-QUICKPROG, 8: «первый запуск — с подтверждением и без
// автоматического старта»; 5 step 11).
//
// The save of a quick program arms the flag for the file it wrote; the first ПУСК of that file only
// asks, and the second press within `START_CONFIRM_MS` starts the machine. The flag lives in
// `MachineState` (`camStartConfirmed`), not in `CamState`: the operator closes the overlay, looks at
// the file, starts it — closing the overlay must not forget that the program is new.
//
// The question is asked in the cycle reducer before the workplace dispatches the start, so AUTO, the
// PROGON and the probe arms are covered by one rule. MDI is not a program and is not asked; a HOLD
// resume is not a new start either.
import type { MachineState } from '../types'
import type { CycleAction } from './cycle-actions'

/** The window the second press of ПУСК counts as the confirmation: five seconds. */
export const START_CONFIRM_MS = 5_000

/** The armed confirmation: the saved file it belongs to and the moment of the first press. */
export interface CamStart {
  name: string
  /** The moment of the first press (epoch ms from the action), or null before the question. */
  pendingStart: number | null
}

/** The file the operator would start: the active one, by name. */
export const startFile = (s: MachineState): string => s.files[s.activeFile]?.name ?? ''

/** One answer of the question: the state to continue from, and whether the run may start. */
export interface Answer {
  s: MachineState
  /** False when the question itself answered the action: the press asked and the run does not start. */
  go: boolean
}

/** The armed confirmation of this press, when the press is a start of that file at all. */
const armedFor = (s: MachineState): { armed: CamStart; at: number | null } | null => {
  const armed = s.camStartConfirmed ?? null
  if (armed === null || armed.name !== startFile(s)) return null
  // The probe runs an arm of its own: the confirmation is about a program the operator saved, and a
  // measurement near the part is not one of them.
  if (s.probe.open && s.screen === 'offset') return null
  return { armed, at: null }
}

/**
 * The confirmation for this press, or null when the run may go on. The first press arms the flag
 * with its moment; a press within `START_CONFIRM_MS` of it is the answer. Without a stamp on the
 * action the moment is unknown and the question is asked again (and never expires).
 */
export function startQuestion(s: MachineState, a: { at?: number }): Answer | null {
  const found = armedFor(s)
  if (found === null) return null
  const { armed } = found
  const at = a.at ?? null
  if (
    armed.pendingStart !== null &&
    at !== null &&
    at >= armed.pendingStart &&
    at - armed.pendingStart <= START_CONFIRM_MS
  )
    return { s: { ...s, camStartConfirmed: null }, go: true }
  return { s: { ...s, camStartConfirmed: { name: armed.name, pendingStart: at } }, go: false }
}

/** The words of the question, or null when the press is not a question. */
export const questionText = (answer: Answer | null): string | null =>
  answer === null || answer.go ? null : 'Новая программа: проверьте её и нажмите Старт ещё раз'

export type { CycleAction }
