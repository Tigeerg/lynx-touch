// The rest-machining arm (PLAN-QUICKPROG 7.6, Q5): the pocket that takes only what the earlier
// pockets left.
//
// The other Q5 arms live in `ops-kinds-q5.ts`; this one is apart because its model names the
// document: `source` is a list of the document indexes whose removed area this operation skips. A
// rest operation keeps its own footprint (`rect`) and the stock of the walls (`allowanceMm`), so
// `ops.ts` re-exports the type and the union narrows.
import type { OperationBase, Rect } from './ops'

/**
 * Rest machining (`rest`, PLAN-QUICKPROG 7.6): the raster covers the footprint minus the area the
 * `source` operations removed before it, grown by the mill radius the way a raster is. An empty
 * `source` is not "nothing removed" but "not asked": the planner cuts the whole footprint and the
 * check says so out loud (a warning, never silence). A source that is not a pocket is skipped with a
 * warning; an index outside the list is an error of the form.
 */
export interface RestOperation extends OperationBase {
  kind: 'rest'
  /** The document indexes of the operations whose removed area is taken out; may be empty. */
  source: number[]
  /** The footprint the rest is looked for in, mm; empty (`w` or `h` zero) means the whole stock. */
  rect: Rect
  /** Extra stock on every cell of the raster beyond `D / 2`, mm. */
  allowanceMm: number
  /** The step between two rows of the raster, % of the diameter. */
  stepOverPct: number
}
