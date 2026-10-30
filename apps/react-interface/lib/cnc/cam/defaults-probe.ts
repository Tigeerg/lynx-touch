// The probe block a new form starts from (PLAN-QUICKPROG, 7.6, «Блок щупа»).
//
// A probe is not a cut: no depth, no step, no spindle of its own. The tool is a probe of the tool
// table, named by the operator like any tool; the touch feed is slow enough to stop on the surface
// and the range is the travel of the search. The list of points starts empty and the check names
// the empty list, so the operator sees a form that has to be filled, not a silent default point.
import type { ProbeOperation } from './ops'
import { DEFAULT_PROBE_RANGE_MM, TOUCH_FEED_MM_MIN } from './probe-defaults'
import type { Material } from './feeds'
import type { NewOperation } from './operation-init'
import { common } from './defaults-common'
import type { ToolRef } from './ops'

/** A new probe block over the frame of the form. */
export const newProbe = (init: NewOperation, tool: ToolRef, material: Material): ProbeOperation => {
  const fields = { ...common(init, tool, material, null), depthMm: 0, stepDownMm: 0 }
  return {
    ...fields,
    kind: 'probe',
    cutting: { ...fields.cutting, plungeFeedMmMin: TOUCH_FEED_MM_MIN },
    points: [],
    rangeMm: DEFAULT_PROBE_RANGE_MM,
    touchFeedMmMin: TOUCH_FEED_MM_MIN,
    axis: 'Z',
  }
}
