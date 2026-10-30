// The rest-machining rules (PLAN-QUICKPROG 7.6, Q5): a source index, a footprint, and what is left.
//
// Two things are errors of the form and block the save:
//   * a `source` index outside the operations list, or naming this very operation (it has not cut
//     anything yet);
//   * an `allowanceMm` below zero, which would take the mill inside the footprint wall.
// Everything else is a warning, because the document still plans: an empty `source` cuts the whole
// footprint ("Нет исходных операций"), a source that is not a pocket is skipped, and a footprint
// the earlier pockets already took plans zero moves and is marked as an empty operation.
import { fmt } from './feeds'
import type { Operation, QuickProgram } from './ops'
import type { Problem } from './validate-common'
import { add } from './validate-common'
import { approximationOf, removedArea, restAreas } from './rest-areas'

type Rest = Extract<Operation, { kind: 'rest' }>

/** The words of the kinds that leave no pocket: the reason of the skipped-source warning. */
const NOT_A_POCKET = 'не оставляет кармана: пропущена'

/**
 * The blocking rules of a rest operation. `index` is its own place in the document, so a source
 * that names the rest itself is refused before the restore is planned from an area it has not cut.
 */
export function checkRest(program: QuickProgram, op: Rest, index: number, out: Problem[]): void {
  if (!(op.allowanceMm >= 0)) {
    add(out, 'allowanceMm', `Припуск остатка ${fmt(op.allowanceMm)} мм отрицательный: траектория войдёт в стенку.`)
    return
  }
  if (op.rect.w < 0 || op.rect.h < 0) {
    add(out, 'rect.w', 'Область остатка вырождена: ширина или высота меньше нуля.')
    return
  }
  for (const source of op.source) {
    if (!Number.isInteger(source) || source < 0 || source >= program.operations.length) {
      add(
        out,
        'source',
        `Исходная операция ${String(source)} вне списка: в документе ${String(program.operations.length)} операций.`,
      )
      return
    }
    if (source === index) {
      add(out, 'source', `Исходная операция ${String(source + 1)} — это сам остаток: выберите предыдущие карманы.`)
      return
    }
  }
}

/**
 * The warnings of a rest operation that do not block the save: the empty source, the source that
 * cuts no pocket, the circle or slot whose area is its box, and the footprint already taken.
 */
export function restWarnings(program: QuickProgram, op: Rest): Problem[] {
  const out: Problem[] = []
  if (op.source.length === 0) {
    out.push({
      field: 'source',
      reason: 'Нет исходных операций: остаток не определён, программа пойдёт по всей площади',
    })
  }
  for (const source of op.source) {
    const previous = program.operations[source]
    if (previous === undefined) continue
    if (!previous.enabled || removedArea(previous) === null) {
      out.push({ field: 'source', reason: `Операция ${String(source + 1)} ${NOT_A_POCKET}` })
    }
  }
  const approximation = approximationOf(program.operations, op.source)
  if (approximation === 'circle') {
    out.push({
      field: 'source',
      reason: 'Круглый карман учтён прямоугольником вокруг круга: остаток по углам приближён',
    })
  } else if (approximation === 'slot') {
    out.push({
      field: 'source',
      reason: 'Паз учтён прямоугольником вокруг фрезы: остаток по краям приближён',
    })
  }
  if (op.source.length > 0 && restAreas(op, program.operations, program.stock).length === 0) {
    out.push({ field: 'source', reason: 'Остатка нет: предыдущие операции уже сняли площадь' })
  }
  return out
}
