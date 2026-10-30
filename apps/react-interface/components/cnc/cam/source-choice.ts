// The sources of a rest operation as a pick (PLAN-QUICKPROG, 7.6, «Остаточная обработка»): the
// operations of the document that leave a pocket behind, one row per operation with its number.
//
// The field `source` is a text of indexes (`0 2`), as the `.qprog` carries them, and the reader of
// the form is `sourceIndexes` in `lib/cnc/cam/form-fields-q5.ts`: a half-typed list keeps the
// document. So the rows here only build that text — `0 2`, numbers one above the other — and never
// hold a list of their own. A row is offered for a pocket kind only: a face or a thread has no
// pocket to take the rest of.
import type { Operation, OperationKind } from '@/lib/cnc/cam/ops'

/** The kinds that leave a pocket behind: the rest of these is what a rest operation cuts. */
export const SOURCE_KINDS: readonly OperationKind[] = ['pocket', 'pocket-island', 'circle-pocket', 'slot']

/** One offered operation: its zero-based index, the one-based number and the kind. */
export interface SourceOption {
  index: number
  number: number
  kind: OperationKind
}

/** The operations of the document a rest can take its sources from, in list order. */
export function sourceOptions(operations: Operation[]): SourceOption[] {
  const out: SourceOption[] = []
  operations.forEach((op, index) => {
    if (SOURCE_KINDS.includes(op.kind)) out.push({ index, number: index + 1, kind: op.kind })
  })
  return out
}

/** The picked indexes of the field text, as `sourceIndexes` reads them back (`0 2 0` is `0 2`). */
export function pickedSources(text: string): number[] {
  const out: number[] = []
  for (const word of text.split(/[\s,;]+/)) {
    if (!/^\d+$/.test(word)) continue
    const value = Number(word)
    if (!Number.isSafeInteger(value) || out.includes(value)) continue
    out.push(value)
  }
  return out
}

/** The field text of the picked indexes: one per line, as `sourceText` writes it. */
export const sourcesText = (indexes: number[]): string => [...new Set(indexes)].join('\n')

/** The text after a press on the row: the index added or dropped, the rest kept in order. */
export function toggleSource(text: string, index: number): string {
  const picked = pickedSources(text)
  const next = picked.includes(index) ? picked.filter((i) => i !== index) : [...picked, index]
  return sourcesText(next)
}
