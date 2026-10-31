// «Взять с DRO» (PLAN-QUICKPROG, 5 step 2 and 6): a field of the form takes the work coordinate.
//
// The display shows work coordinates (`toWork`, `coords.ts`), so the field takes exactly the number
// the operator reads. The reducer writes it through the same `edited` path a typed value takes, so
// the check and the debounced preview behave the same; this module only says which value of the
// form a field edits and what the number is.
//
// The fields are read from the form itself, not from a table per kind: the axes of a shape are
// named `*.x` by every kind, and the bare `x`/`y`/`z` of the engraving and the face is as good.
import type { Axes } from '../types'
import type { CamState } from './types'

/** The fields that may take a coordinate; the action's `field` is one of them. */
export const DRO_FIELDS = ['x', 'y', 'topZ'] as const

export type DroField = (typeof DRO_FIELDS)[number]

/** The axis a field reads: the top of the material is the Z of the display. */
const AXIS: Record<DroField, keyof Axes> = { x: 'X', y: 'Y', topZ: 'Z' }

/** Whether the field is one the form may take a coordinate into. */
export const isDroField = (field: string): field is DroField => (DRO_FIELDS as readonly string[]).includes(field)

/**
 * The name of the form value a field edits, or null when the form of the kind has none. The exact
 * name wins, then the first `*.x` of the form; `topZ` has only its exact name, because a shape has
 * no other top of the material.
 */
export const droField = (cam: CamState, field: string): string | null => {
  if (cam.form === null || !isDroField(field)) return null
  const { values } = cam.form
  if (field in values) return field
  if (field === 'topZ') return null
  return Object.keys(values).find((key) => key.endsWith(`.${field}`)) ?? null
}

/** The work coordinate the field takes, as typed text: two decimals, the display's own rounding. */
export const droText = (work: Axes, field: DroField): string => String(Math.round(work[AXIS[field]] * 100) / 100)
