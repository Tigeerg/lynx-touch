// The safety rules of PLAN-QUICKPROG, section 8 (PLAN-QUICKPROG, 7.4).
//
// The entry point and the public types: `validate` returns `{ ok: true }` or
// `{ ok: false, bad: { field, reason }[] }`. The rules themselves are split by meaning: the common
// ones (tool, cutting data, depth, step) in `validate-common.ts`, the shape rules of every kind in
// `validate-shape.ts`, the program ones (safe Z, G64) in `validate-program.ts`.
//
// Every problem is bound to the field the operator sees, so the form shows the reason next to the
// input rather than on one line at the bottom. The texts are the ones of the console of
// `apps/quickprog`, word for word: the panel and the demo tell the operator the same thing.
//
// A list of problems, not the first one: the form points at every wrong field at once and does not
// make the operator fix them one save at a time. The order is fixed, so a form can scroll to the
// first problem and a test can compare the first field: the program (WCS, the empty list) first,
// then per operation tool, cutting data, depth, step, shape and peck, then safe Z and G64.
export type { Problem, Validation } from './validate-common'
export { EPS, MIN_PECK_MM, MIN_STEP_MM, WCS_WORDS, add, numberFields, tableRowFor, validate } from './validate-common'
export type { NumberField } from './validate-common'
export { checkGeometry, checkPeck } from './validate-shape'
export { probeValidation, probeWarnings } from './probe'
export { slotWarnings } from './validate-slot'
export { counterboreWarnings, DEFAULT_DWELL_SEC } from './validate-counterbore'
export { RIGID_TAPPING_PARAM, isStandardPitch, STANDARD_PITCHES_MM, tapWarnings } from './validate-tap'
export { checkSafeZ, checkTolerance } from './validate-program'
