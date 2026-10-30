// The two small dialogs of an operation card — «Массив» and «Поворот» (PLAN-QUICKPROG, 7.6).
//
// The project has no DOM renderer, so the part that can be wrong is pure here and the dialogs in
// `array-dialog.tsx`/`rotate-dialog.tsx` only draw it: the typed strings become the numbers of the
// `CAM_ARRAY`/`CAM_ROTATE` action, a field that is not a number is named instead of guessed, and a
// copy count over the limit of the document is said before the action leaves the dialog. The
// refusal of the document itself (`copy-ops.ts`) stays the last word: this module only says what
// the numbers mean and what the dialog already knows is too much.
import { Grid3x3, RotateCw, type LucideIcon } from 'lucide-react'
import { MAX_COPIES } from '@/lib/cnc/cam/copy-ops'
import type { CamAction } from '@/lib/cnc/cam/cam'

/** The two copying actions of a card, as the menu before the dialogs names them. */
export const COPY_MENU: { kind: 'array' | 'rotate'; label: string; icon: LucideIcon }[] = [
  { kind: 'array', label: 'Массив', icon: Grid3x3 },
  { kind: 'rotate', label: 'Поворот', icon: RotateCw },
]

/** The fields of the grid dialog, in the order the operator reads them. */
export const ARRAY_FIELDS: { key: string; label: string; unit: string; hint: string }[] = [
  { key: 'cols', label: 'Столбцов', unit: 'шт', hint: 'Копий по X, вместе с исходной' },
  { key: 'rows', label: 'Рядов', unit: 'шт', hint: 'Копий по Y, вместе с исходной' },
  { key: 'dx', label: 'Шаг по X', unit: 'мм', hint: 'Смещение соседнего столбца' },
  { key: 'dy', label: 'Шаг по Y', unit: 'мм', hint: 'Смещение соседнего ряда' },
]

/** The fields of the turn dialog, in the order the operator reads them. */
export const ROTATE_FIELDS: { key: string; label: string; unit: string; hint: string }[] = [
  { key: 'angleDeg', label: 'Угол', unit: '°', hint: 'Шаг между копиями; карман — только кратный 90' },
  { key: 'cx', label: 'Центр X', unit: 'мм', hint: 'Точка, вокруг которой поворачиваются копии' },
  { key: 'cy', label: 'Центр Y', unit: 'мм', hint: 'Точка, вокруг которой поворачиваются копии' },
  { key: 'copies', label: 'Число копий', unit: 'шт', hint: 'Без исходной операции' },
]

/** The typed text of the grid dialog, as the dialog holds it. */
export type ArrayText = Record<'cols' | 'rows' | 'dx' | 'dy', string>
/** The typed text of the turn dialog, as the dialog holds it. */
export type RotateText = Record<'angleDeg' | 'cx' | 'cy' | 'copies', string>
/** The numbers of either dialog, keyed as the text is. */
type Numbers = Record<string, number>

/** The default text of the grid: four copies `20 x 20` mm away from the source at (0, 0). */
export const ARRAY_DEFAULT: ArrayText = { cols: '2', rows: '2', dx: '20', dy: '20' }
/** The default text of the turn: four copies a quarter of a turn around the part origin. */
export const ROTATE_DEFAULT: RotateText = { angleDeg: '90', cx: '0', cy: '0', copies: '3' }

/** A field and its text that is not a number; `key` is the form-name of the dialog. */
export interface TextProblem {
  key: string
  reason: string
}

/** The action the grid dialog sends, or the first field that is not a number. */
export function arrayAction(
  index: number,
  text: ArrayText,
): { ok: true; action: CamAction } | { ok: false; problem: TextProblem } {
  const parsed = numbers(text, ARRAY_FIELDS)
  if (!parsed.ok) return parsed
  const { cols, rows, dx, dy } = parsed.values
  return { ok: true, action: { type: 'CAM_ARRAY', index, cols: cols ?? 0, rows: rows ?? 0, dx: dx ?? 0, dy: dy ?? 0 } }
}

/** The action the turn dialog sends, or the first field that is not a number. */
export function rotateAction(
  index: number,
  text: RotateText,
): { ok: true; action: CamAction } | { ok: false; problem: TextProblem } {
  const parsed = numbers(text, ROTATE_FIELDS)
  if (!parsed.ok) return parsed
  const { angleDeg, cx, cy, copies } = parsed.values
  return {
    ok: true,
    action: { type: 'CAM_ROTATE', index, angleDeg: angleDeg ?? 0, cx: cx ?? 0, cy: cy ?? 0, copies: copies ?? 0 },
  }
}

/**
 * What the dialog itself knows about the numbers before the document sees them: a count that is
 * not a whole number, or a total over `MAX_COPIES`, is a refusal line, not an action that the
 * reducer then refuses. The rest — a kind that is not copied, a pocket off a quarter-turn — is the
 * document's word (`copy-ops.ts`) and comes back as the overlay message.
 */
export function dialogRefusal(action: CamAction): TextProblem | null {
  if (action.type === 'CAM_ARRAY') {
    const total = action.cols * action.rows
    if (!whole(action.cols) || !whole(action.rows)) return { key: 'cols', reason: 'Число копий — целое больше нуля.' }
    if (total < 2) return { key: 'cols', reason: 'Сетка 1 на 1 не даёт копий: задайте больше столбцов или рядов.' }
    if (total > MAX_COPIES) return { key: 'rows', reason: `Слишком много копий: не больше ${String(MAX_COPIES)}.` }
    return null
  }
  if (action.type === 'CAM_ROTATE') {
    if (!whole(action.copies)) return { key: 'copies', reason: 'Число копий — целое больше нуля.' }
    if (action.copies + 1 > MAX_COPIES) {
      return { key: 'copies', reason: `Слишком много копий: не больше ${String(MAX_COPIES)} вместе с исходной.` }
    }
    return null
  }
  return null
}

/** True for a whole number above zero; a fraction or a zero is not a count. */
const whole = (value: number): boolean => Number.isInteger(value) && value > 0

/** The numbers of the text, or the first field that is not a number and its empty text. */
const numbers = (
  text: Record<string, string>,
  fields: { key: string; label: string }[],
): { ok: true; values: Numbers } | { ok: false; problem: TextProblem } => {
  const values: Numbers = {}
  for (const field of fields) {
    const raw = (text[field.key] ?? '').trim().replace(',', '.')
    const value = raw === '' ? Number.NaN : Number(raw)
    if (!Number.isFinite(value)) {
      return { ok: false, problem: { key: field.key, reason: `Поле «${field.label}» не число: введите число.` } }
    }
    values[field.key] = value
  }
  return { ok: true, values }
}
