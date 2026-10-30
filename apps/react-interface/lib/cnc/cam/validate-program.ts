// The program-level rules: safe Z and the G64 tolerance (PLAN-QUICKPROG, 8 and 7.3).
//
// They look at the document, not at the shape of an operation, and they run after the operations,
// as the console does. The reasons are the texts of the console of `apps/quickprog`.
import { fmt } from './feeds'
import type { QuickProgram } from './ops'
import { stockTop } from './ops'
import type { Problem } from './validate-common'
import { EPS, add } from './validate-common'

/** Safe Z has to be above the top of the stock; an equal Z is not above it. */
export function checkSafeZ(program: QuickProgram, out: Problem[]): void {
  const top = stockTop(program.stock)
  if (!(program.safeZ > top)) {
    add(out, 'safeZ', `Безопасный Z ${fmt(program.safeZ)} мм не выше верха заготовки ${fmt(top)} мм.`)
  }
}

/** A G64 tolerance: positive and not over a millimetre, else the control cuts the corners off. */
/** A G64 tolerance: positive and not over a millimetre, else the control cuts the corners off. */
export function checkTolerance(out: Problem[], field: string, value: number): void {
  const letter = field.charAt(field.length - 1)
  if (!(value > 0)) {
    add(out, field, `Допуск G64 ${letter} = ${fmt(value)} мм: допуск должен быть больше нуля.`)
  } else if (value > 1 + EPS) {
    add(out, field, `Допуск G64 ${letter} = ${fmt(value)} мм больше 1 мм: сглаживание уведёт фрезу от контура.`)
  }
}
