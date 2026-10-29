// The cycle subsystem's action union and the predicate that claims it. The registry (machine.ts)
// composes the subsystem through them; the reducer that switches over the union lives in
// cycle-reducer.ts, and cycle.ts is the facade.

export type CycleAction =
  | { type: 'TICK'; dt: number }
  | { type: 'CYCLE_START' }
  | { type: 'FEED_HOLD' }
  | { type: 'RESET' }
  | { type: 'ESTOP'; at?: number }
  | { type: 'SPINDLE_WARMUP' }

const CYCLE_TYPES = new Set<string>(['TICK', 'CYCLE_START', 'FEED_HOLD', 'RESET', 'ESTOP', 'SPINDLE_WARMUP'])

export const isCycleAction = (a: { type: string }): a is CycleAction => CYCLE_TYPES.has(a.type)
