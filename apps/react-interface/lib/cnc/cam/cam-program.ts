// The program of the overlay edited from its header (PLAN-QUICKPROG, 5 steps 1-2): the work offset,
// the stock box, the safe Z and the name change in the document itself, not in the form.
//
// The header cells are one table: a `field` names a value of the program, the typed text lands in
// it, the safe Z follows the top of the stock while the operator did not type one of its own, and
// the document runs through the same `validate` the form and the save read, so the reason of a
// wrong number appears in the header's line with the field it talks about.
import type { QuickProgram, StockBox } from './ops'
import { stockTop } from './ops'
import { SAFE_Z_CLEARANCE_MM } from './defaults'
import type { Problem } from './validate'
import { validate, WCS_WORDS } from './validate'

/** The result of one header edit: the new document and the first problem it has, if any. */
export interface ProgramEdit {
  program: QuickProgram
  problem: Problem | null
}

/** The axes of the stock box, in the order the header shows them. */
const CORNERS = ['min.x', 'max.x', 'min.y', 'max.y', 'min.z', 'max.z'] as const

export type CornerField = `stock.${(typeof CORNERS)[number]}`

/** The fields of the header; anything else is a key nobody edits. */
export const PROGRAM_FIELDS: readonly string[] = ['wcs', 'safeZ', 'name', ...CORNERS.map((c) => `stock.${c}`)]

/** A number from typed text: the comma of the keyboard is a decimal point, `NaN` stays `NaN`. */
const number = (text: string): number => {
  const value = Number(text.trim().replace(',', '.'))
  return text.trim() === '' ? Number.NaN : value
}

/** The document with one corner replaced; the box keeps the values it had for the other corners. */
const withCorner = (stock: StockBox, corner: string, value: number): StockBox => {
  const [end, axis] = corner.split('.') as ['min' | 'max', 'x' | 'y' | 'z']
  return { ...stock, [end]: { ...stock[end], [axis]: value } }
}

/** Whether the edited field is a corner of the stock box. */
const isCorner = (field: string): field is CornerField => CORNERS.some((c) => field === `stock.${c}`)

/**
 * The document with `field` set to the typed `value`, or null when the header has no such field.
 * The safe Z follows the top of the stock when the operator types the stock and not safe Z: the
 * clearance is a number the header offers, not a hidden constant of the old stock.
 */
export function programEdit(program: QuickProgram, field: string, value: string): ProgramEdit | null {
  if (field === 'name') {
    return { program: { ...program, name: value }, problem: null }
  }
  if (field === 'wcs') {
    const wcs = value.trim().toUpperCase()
    if (!(WCS_WORDS as readonly string[]).includes(wcs)) {
      const reason = `Смещение ${value.trim() || '—'} не найдено: выберите G54..G59.`
      return { program, problem: { field: 'wcs', reason } }
    }
    const next: QuickProgram = { ...program, wcs: wcs as QuickProgram['wcs'] }
    const checked = validate(next)
    return { program: next, problem: checked.ok ? null : (checked.bad[0] ?? null) }
  }
  if (field === 'safeZ') {
    const safeZ = number(value)
    const next: QuickProgram = { ...program, safeZ }
    const checked = validate(next)
    return { program: next, problem: checked.ok ? null : (checked.bad[0] ?? null) }
  }
  if (isCorner(field)) {
    const corner = field.slice('stock.'.length)
    const stock = withCorner(program.stock, corner, number(value))
    const next: QuickProgram = { ...program, stock, safeZ: stockTop(stock) + SAFE_Z_CLEARANCE_MM }
    const checked = validate(next)
    return { program: next, problem: checked.ok ? null : (checked.bad[0] ?? null) }
  }
  return null
}
