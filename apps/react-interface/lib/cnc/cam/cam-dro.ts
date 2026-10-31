// «Взять с DRO» (PLAN-QUICKPROG, 5 step 2 and 6): a field of the form takes the work coordinate.
//
// The display shows work coordinates (`toWork`, `coords.ts`), so the field takes exactly the number
// the operator reads. The reducer writes it through the same `edited` path a typed value takes, so
// the check and the debounced preview behave the same; this module only says which value of the
// form a field edits and what the number is.
//
// The action names the exact form value (`rect.x`, `hole.x`, `x0`, the bare `x` of an engraving),
// not an axis: the grid of a drill has `hole.x` and `x0` in one form, and a suffix search would write
// the first of them, not the one under the button. The axis the number comes from is decided by the
// name itself, so the two cannot disagree.
import type { Axes } from '../types'
import type { CamState } from './types'

/** The top of the material is the only `topZ`; every other field ends in its axis letter. */
const TOP_FIELD = 'topZ'

/**
 * The axis a form value reads: the Z of the top of the material, the Y of a `*.y`/`y`, the X of a
 * `*.x`/`x`, and the axes of the grid origin (`x0`, `y0`) of a hole layout. A name that is none of
 * them is a size, not a position, and the caller refuses it.
 */
export const axisOfField = (field: string): keyof Axes | null => {
  if (field === TOP_FIELD) return 'Z'
  if (field === 'y' || field === 'y0' || field.endsWith('.y')) return 'Y'
  if (field === 'x' || field === 'x0' || field.endsWith('.x')) return 'X'
  return null
}

/** The form value the action names, or null when the form of the kind has none. */
export const droField = (cam: CamState, field: string): string | null => {
  if (cam.form === null) return null
  const axis = axisOfField(field)
  if (axis === null) return null
  const values = cam.form.values
  if (field in values) return field
  // The form names its shape axes `rect.x`, `hole.y`: the bare axis of the display is that name.
  const suffix = axis === 'Z' ? null : `.${axis.toLowerCase()}`
  if (suffix === null) return null
  return Object.keys(values).find((key) => key.endsWith(suffix)) ?? null
}

/** The work coordinate of `field`, as typed text: two decimals, the display's own rounding. */
export const droText = (work: Axes, field: string): string => {
  const axis = axisOfField(field)
  return axis === null ? '' : String(Math.round(work[axis] * 100) / 100)
}
