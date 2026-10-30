// The common rules every operation shares (PLAN-QUICKPROG, 8 and 7.4).
//
// Tool, cutting data, depth, step, safe Z and the G64 tolerances: the checks that do not look at
// the shape of the operation. The form runs them after every keystroke, before a path exists; the
// planner may assume a valid document. A problem is bound to the field the operator sees, and the
// reason is the text of the console of `apps/quickprog`, word for word.
//
// The limits live in their own modules, the number table in `validate-numbers.ts`; this file holds
// the guard clauses and keeps the exports its callers already import.
import { fmt } from './feeds'
import { MACHINE_LIMITS } from './limits'
import type { Operation, QuickProgram } from './ops'
import { stockBottom, stockTop } from './ops'
import { numberFields, tableRowFor } from './validate-numbers'
import { checkGeometry, checkPeck } from './validate-shape'
import { checkSafeZ, checkTolerance } from './validate-program'

export type { NumberField } from './validate-numbers'
export { numberFields, tableRowFor } from './validate-numbers'

/** One problem of the form: the field path and the reason in the operator's words. */
export interface Problem {
  field: string
  reason: string
}

/** The result of a check: either no problems, or a non-empty list of them. */
export type Validation = { ok: true } | { ok: false; bad: Problem[] }

/** The WCS words the post-processor and the check share. */
export const WCS_WORDS = ['G54', 'G55', 'G56', 'G57', 'G58', 'G59'] as const

/** The tolerance the numbers compare at: a millimetre is a coarse unit of a toolpath. */
export const EPS = 1e-9
/** The smallest axial step worth a pass, mm. */
export const MIN_STEP_MM = 0.1
/** The smallest peck Q of G83 worth a cycle, mm. */
export const MIN_PECK_MM = 0.1
/** A feed over this many table rows is a mode mistake, not a correction. */
const FEED_OVER_TABLE = 3

/** Append one problem bound to the field of the form. */
export const add = (out: Problem[], field: string, reason: string): void => {
  out.push({ field, reason })
}

/** All problems of the program in a fixed order; an empty list means it can be planned. */
export function validate(program: QuickProgram): Validation {
  const bad: Problem[] = []
  if (!(WCS_WORDS as readonly string[]).includes(program.wcs)) {
    bad.push({ field: 'wcs', reason: `Смещение ${program.wcs} не найдено: выберите G54..G59.` })
  }
  if (program.operations.length === 0) {
    bad.push({ field: 'operations', reason: 'В программе нет ни одной операции.' })
    return finish(bad)
  }
  for (const op of program.operations) {
    if (!op.enabled) continue
    // A number of the document that is not a number is the clearest wrong field there is: a
    // half-typed form (`stepOver` = "abc") is refused by name, and the checks below, which compare
    // with `<=`, see `NaN` as false and would let it through to the planner and the emitter.
    checkNumbers(op, bad)
    checkTool(op, bad)
    // A custom block has no cut of its own: its spindle and feed code is in its own lines.
    if (op.kind !== 'custom') checkCutting(op, bad)
    checkDepth(program, op, bad)
    checkStep(op, bad)
    checkGeometry(program, op, bad)
    checkPeck(op, bad)
  }
  checkSafeZ(program, bad)
  checkTolerance(bad, 'g64P', program.g64P)
  checkTolerance(bad, 'g64Q', program.g64Q)
  return finish(bad)
}

const finish = (bad: Problem[]): Validation => (bad.length === 0 ? { ok: true } : { ok: false, bad })

/**
 * A number of the document that is not a number: the field is named and the other checks go on. */
function checkNumbers(op: Operation, out: Problem[]): void {
  for (const item of numberFields(op)) {
    // The problem names the rule field (`geometry`, `stepOver`) as the other checks do; the form
    // finds the input by the words, as it does for the reasons of `validate-shape.ts`.
    if (!Number.isFinite(item.value)) add(out, item.field, `Поле «${item.label}» не число: введите число.`)
  }
}

function checkTool(op: Operation, out: Problem[]): void {
  // A custom block may name no tool: it changes nothing and prints no UI_TOOL, which `viewerIssues`
  // warns about. Every other kind cuts with a tool and needs the row of the table.
  if (op.kind === 'custom') return
  if (op.tool.number <= 0) add(out, 'tool.number', 'Инструмент не выбран: укажите номер в таблице инструмента.')
  if (op.tool.hNumber < 0) {
    add(
      out,
      'tool.hNumber',
      `Номер коррекции H ${op.tool.hNumber} отрицательный: в таблице коррекций нет такого номера.`,
    )
  }
  const { minToolDiameterMm: min, maxToolDiameterMm: max } = MACHINE_LIMITS
  if (op.tool.diameterMm <= 0) {
    add(out, 'tool.diameterMm', 'Диаметр инструмента должен быть больше нуля.')
  } else if (op.tool.diameterMm < min || op.tool.diameterMm > max) {
    add(
      out,
      'tool.diameterMm',
      `Диаметр ${fmt(op.tool.diameterMm)} мм вне станка: допустимо от ${fmt(min)} до ${fmt(max)} мм.`,
    )
  }
}

function checkCutting(op: Operation, out: Problem[]): void {
  const table = tableRowFor(op)
  if (op.cutting.spindleRpm <= 0) {
    add(out, 'cutting.spindleRpm', 'Обороты шпинделя должны быть больше нуля.')
  } else if (op.cutting.spindleRpm > MACHINE_LIMITS.maxRpm) {
    add(
      out,
      'cutting.spindleRpm',
      `Обороты ${fmt(op.cutting.spindleRpm)} об/мин выше максимума станка ${fmt(MACHINE_LIMITS.maxRpm)} об/мин.`,
    )
  }
  const feed = op.cutting.feedMmMin
  if (feed <= 0) {
    add(out, 'cutting.feedMmMin', 'Рабочая подача должна быть больше нуля.')
  } else if (feed > MACHINE_LIMITS.maxFeedMmMin) {
    add(
      out,
      'cutting.feedMmMin',
      `Подача ${fmt(feed)} мм/мин выше максимума станка ${fmt(MACHINE_LIMITS.maxFeedMmMin)} мм/мин.`,
    )
  } else if (table.feedMmMin > 0 && feed > table.feedMmMin * FEED_OVER_TABLE) {
    add(
      out,
      'cutting.feedMmMin',
      `Подача ${fmt(feed)} мм/мин втрое выше табличной (${table.source}: ${fmt(table.feedMmMin)}). Проверьте режим.`,
    )
  }
  // A drill has one feed by construction: a difference there is the normalisation of the model,
  // not a mistake of the operator, so the plunge rule is a mill rule only.
  if (op.kind === 'drill') return
  const plunge = op.cutting.plungeFeedMmMin
  if (plunge <= 0) {
    add(out, 'cutting.plungeFeedMmMin', 'Подача врезания должна быть больше нуля.')
  } else if (plunge > feed && feed > 0) {
    add(out, 'cutting.plungeFeedMmMin', 'Подача врезания не должна быть выше рабочей подачи.')
  }
}

function checkDepth(program: QuickProgram, op: Operation, out: Problem[]): void {
  // A custom block has no depth of its own: its Z words are in its lines, the viewer checks them.
  if (op.kind === 'custom') return
  const top = stockTop(program.stock)
  const bottom = stockBottom(program.stock)
  if (op.depthMm <= 0) {
    add(out, 'depthMm', 'Глубина обработки должна быть больше нуля.')
    return
  }
  if (op.depthMm > top - bottom + EPS && op.topZ <= top + EPS) {
    add(out, 'depthMm', `Глубина ${fmt(op.depthMm)} мм ниже дна заготовки: снимается не более ${fmt(top - bottom)} мм.`)
  }
  if (op.topZ - op.depthMm < bottom - EPS) {
    add(
      out,
      'depthMm',
      `Дно прохода ${fmt(op.topZ - op.depthMm)} мм ниже дна заготовки ${fmt(bottom)} мм: уменьшите глубину.`,
    )
  }
}

function checkStep(op: Operation, out: Problem[]): void {
  // A drill and a tap descend in one cycle and never split the hole; a zero step is their normal
  // value. A negative step is a mistake of any kind. A custom block has no step: its lines are its
  // own program.
  if (op.kind === 'custom') return
  if ((op.kind === 'drill' || op.kind === 'tap') && op.stepDownMm >= 0) return
  if (!(op.stepDownMm > 0)) {
    add(out, 'stepDownMm', 'Шаг врезания должен быть больше нуля: иначе проходов не будет.')
  } else if (op.stepDownMm > op.depthMm && op.depthMm > 0) {
    add(
      out,
      'stepDownMm',
      `Шаг врезания ${fmt(op.stepDownMm)} мм больше глубины ${fmt(op.depthMm)} мм: проход будет один, но проверьте задание.`,
    )
  }
}
