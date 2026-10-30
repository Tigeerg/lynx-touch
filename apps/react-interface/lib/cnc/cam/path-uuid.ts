// The identity of a planning result (PLAN-QUICKPROG, 7.4, C2).
//
// The demo numbers its plans `path-000001`, `path-000002`, ... and a stage is `<uuid>/op<n>`. The
// panel keeps the same spelling, so a golden file names the same blocks and a stale preview can be
// told from a fresh one after a debounce. The uuid lives in its own module: the move collector and
// the planner both return a path and must not depend on each other for one string.
/** A plan serial in the demo's spelling: `path-000001`, `path-000002`, ... */
export const nextPathUuid = (serial: number): string => `path-${String(serial).padStart(6, '0')}`

/** The demo's plan uuid of a fresh session: `path-000001`. */
export const SESSION_UUID = nextPathUuid(1)
