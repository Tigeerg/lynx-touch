// The state of one operation form (PLAN-QUICKPROG, 7.4, C5 and 7.6): the open tab, the field the
// touch pad edits, the rows the markup draws and the warnings of the kind. Split from `cam-form.tsx`
// so the markup of a row stays in its own file and the form body reads as a page.
//
// The form keeps no local copy of the numbers: `CamForm.values` in the state is the single source.
// The preview is the exception and lives in the form body (`CAM_PREVIEW` after 150 ms).
import { useState } from 'react'
import type { CamForm } from '@/lib/cnc/cam/form'
import { formOperation, formProblems } from '@/lib/cnc/cam/form'
import { newProgram } from '@/lib/cnc/cam/defaults'
import { errorsByField, fieldGroups, patternFields, type CamTab } from './cam-fields'
import { kindWarnings as kindWarningsOf } from './cam-labels'

/** A hole kind — a drill or a tap — shows the fields of its layout only: a ring has no «Столбцов». */
const isHidden = (key: string, tabKeys: string[], values: Record<string, string>): boolean =>
  tabKeys.includes('pattern') &&
  key !== 'pattern' &&
  key !== 'body' &&
  !patternFields(values.pattern ?? 'hole').includes(key)

/** The parts of the form the markup reads: the open tab, the field the pad edits, the shown rows. */
export function useFormState(form: CamForm) {
  const [tab, setTab] = useState<CamTab>('geometry')
  const [active, setActive] = useState<string | null>(null)
  const errors = errorsByField(form.error ? [form.error] : [])
  // The operation the operator is building, as the rules see it. Its warnings go under the problem
  // line and only once the operation plans: a form that fails the save shows the problem, not a
  // warning on top of it. The check runs on its own document, so it does not read the list.
  const live = formOperation(form, null)
  const kindWarnings =
    formProblems({ ...newProgram(), operations: [] }, form.values, live).length === 0 ? kindWarningsOf(live) : []
  const groups = fieldGroups(form.op, tab)
  const keysOfTab = groups.flatMap((g) => g.fields.map((f) => f.key))
  const others = groups.flatMap((g) => g.fields).filter((f) => errors[f.key] && !keysOfTab.includes(f.key))
  const shown = groups
    .flatMap((g) => g.fields)
    // The layout fields of a hole kind follow the chosen pattern; every other kind shows all.
    .filter(
      (f) =>
        tab !== 'geometry' || (form.op !== 'drill' && form.op !== 'tap') || !isHidden(f.key, keysOfTab, form.values),
    )
  return { tab, setTab, active, setActive, errors, kindWarnings, others, shown }
}
