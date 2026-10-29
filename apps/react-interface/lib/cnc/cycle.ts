// The cycle subsystem, public surface: the block execution (cycle/cycle-exec), the action union
// and its predicate (cycle/cycle-actions) and the reducer (cycle/cycle-reducer). Kept as a facade
// so the importers of './cycle' (machine.ts, nav.ts, subsystem.ts, cycle.test.ts) keep one entry
// point while the modules stay small (CLAUDE.md, section 2).
export * from './cycle/cycle-exec'
export * from './cycle/cycle-actions'
export * from './cycle/cycle-reducer'
