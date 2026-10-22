// Index and range arithmetic shared by the reducers. One source: the per-module copies had
// drifted, and `clamp` alone turned an empty list into a -1 cursor, which travelled on as a
// negative program line and crashed the panel on start (CLAUDE.md, section 3: a silent -1 is
// worse than a guard).

export const clamp = (v: number, min: number, max: number) => Math.min(max, Math.max(min, v))

/**
 * Moves an index by `delta` inside `[0, length - 1]`. An empty list (`length <= 0`) keeps the
 * cursor at 0 and moves nowhere: `clamp(x, 0, -1)` returned -1, and the list read as undefined
 * one step later (the alarm list after "clear all", a program with no lines).
 */
export const clampIndex = (prev: number, delta: number, length: number) =>
  length <= 0 ? 0 : clamp(prev + delta, 0, length - 1)
