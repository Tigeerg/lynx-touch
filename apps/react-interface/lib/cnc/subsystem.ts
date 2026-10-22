// The action layer's leaf module: the result a subsystem returns, the dispatch it uses to hand
// an action back to the machine, the action union itself, and the registry entry. Subsystems
// import these from here instead of machine.ts, so the module graph has no circle and machine.ts
// composes the subsystems instead of absorbing them (CLAUDE.md, section 2). Result was copied in
// every subsystem and Dispatch in two; both have one source here.
import type { ControlAction } from './control'
import type { CycleAction } from './cycle'
import type { EditorAction } from './editor'
import type { LadderAction } from './ladder'
import type { MpgAction } from './mpg'
import type { NavAction } from './nav'
import type { ProbeWizardAction } from './probe-wizard'
import type { ProgAction } from './prog'
import type { ServiceAction } from './service'
import type { SetupAction } from './setup'
import type { TestAction } from './test'
import type { ToolMeasureAction } from './tool-measure'
import type { MachineState, Message } from './types'

/** What a subsystem hands back: the new state and an optional operator message. */
export type Result = { s: MachineState; msg?: string; level?: Message['level'] }

/** The branches machine.ts reduces itself: the console's own keys (alarms, help, theme, ...). */
export type MachineAction =
  | { type: 'ALARM_ACK' }
  | { type: 'ALARM_CLEAR_ALL' }
  | { type: 'HELP'; open?: boolean }
  | { type: 'THEME_TOGGLE' }
  | { type: 'UI_MODE_SET'; value: 0 | 1 }
  | { type: 'TOOLPATH_TOGGLE' }
  | { type: 'CALC'; open?: boolean }

/** Every action the panel can send: the machine's own plus one union per subsystem. */
export type Action =
  | MachineAction
  | ProgAction
  | LadderAction
  | MpgAction
  | TestAction
  | EditorAction
  | SetupAction
  | ControlAction
  | ProbeWizardAction
  | ServiceAction
  | CycleAction
  | NavAction
  | ToolMeasureAction

const MACHINE_TYPES = new Set<string>([
  'ALARM_ACK',
  'ALARM_CLEAR_ALL',
  'HELP',
  'THEME_TOGGLE',
  'UI_MODE_SET',
  'TOOLPATH_TOGGLE',
  'CALC',
])

/** Claims the machine's own branches, the ones no subsystem reduces. */
export const isMachineAction = (a: { type: string }): a is MachineAction => MACHINE_TYPES.has(a.type)

/** The whole-machine reduce the machine passes to a subsystem that delegates an action. */
export type Dispatch = (s: MachineState, a: Action) => MachineState

/** One entry of the machine's registry: reduce the action or pass it on (null). */
export interface Subsystem {
  reduce: (s: MachineState, a: Action, dispatch: Dispatch) => Result | null
}

/**
 * Builds a registry entry from a subsystem's predicate and reducer: an action its predicate does
 * not claim returns null and the registry moves to the next subsystem, so the machine composes a
 * table instead of a chain of `if` (CLAUDE.md, section 6.8: a new subsystem joins through the
 * machine, the machine does not grow).
 */
export function defineSubsystem<A extends Action>(config: {
  claims: (a: { type: string }) => a is A
  reduce: (s: MachineState, a: A, dispatch: Dispatch) => Result
}): Subsystem {
  return {
    reduce: (s, a, dispatch) => (config.claims(a) ? config.reduce(s, a, dispatch) : null),
  }
}
