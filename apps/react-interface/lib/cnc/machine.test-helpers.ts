// Shared helpers of the reducer checks: the machine behaviour is fixed by the machine.*.test.ts
// files, and every one of them runs the same action list from the same starting state. The name
// does not end in `.test.ts`, so vitest does not pick this module up as a file.
import { initialState, reducer, type Action } from './machine'
import type { MachineState } from './types'

export const run = (state: MachineState, ...actions: Action[]) => actions.reduce(reducer, state)
export const from = (...actions: Action[]) => run(initialState, ...actions)
