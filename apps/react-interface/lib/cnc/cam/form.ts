// The form of one operation and the way it maps to the document (PLAN-QUICKPROG, 6 and 7.4).
//
// The form keeps strings, as the input fields do: `values` is what the operator typed, so a value
// that is not a number yet stays in the field instead of being silently replaced. The mapping to
// the operation is explicit in both directions:
//
//   values -> operation   `formOperation` / `applyFields`, fields grouped by meaning
//   operation -> values   `valuesOf`, so CAM_EDIT opens the form with the numbers of the document
//
// The fields live in `form-fields.ts`, the assembly of the operation and its problems in
// `form-operation.ts`; this file is the facade both callers import by name.
export type { CamForm } from './form-fields'
export { applyFields, formValues, newForm, valuesOf } from './form-fields'
export { formOperation, formProblems } from './form-operation'
