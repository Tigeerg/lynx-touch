// The tapping rules (PLAN-QUICKPROG 7.6): a thread needs a machine that can stop inside the hole.
//
// A rigid tap goes down and comes back at the spindle, and `G84` has no way to back out of a hole
// that is already cut: a machine without the feature tears the thread or breaks the tap. So the
// machine parameter `rigid_tapping` is an error, not a warning, and a document without the
// parameter reads the catalogue default (off). The pitch is checked as a number: a non-positive
// one is an error, a standard pitch is silent, and a pitch off the standard series is a warning:
// the thread may still be cut, but the operator has to see that it is not a stock one.
//
// The tool fits the hole by the tap rule `hole = tap - pitch`, not by the thread table: the plan
// has no table of threads (PLAN-QUICKPROG 7.6), so the pair of a pitch and a diameter is the
// operator's business.
import { fmt } from './feeds'
import type { Operation, ProgramShape, QuickProgram, TapOperation } from './ops'
import { paramValue } from '../service-data'
import type { Problem } from './validate-common'
import { EPS, add } from './validate-common'
import { tapHoles } from './tap-holes'

/** The catalogue id of the machine's rigid tapping switch (`params/system.ts`). */
export const RIGID_TAPPING_PARAM = 'EMC.RIGID_TAPPING'

/** The standard coarse pitches of a metric thread, mm: off this series the pitch only warns. */
export const STANDARD_PITCHES_MM: readonly number[] = [
  0.35, 0.4, 0.45, 0.5, 0.6, 0.7, 0.75, 0.8, 1, 1.25, 1.5, 1.75, 2, 2.5, 3,
]

/** True when the pitch is one of the standard series within a thousandth of a millimetre. */
export const isStandardPitch = (pitchMm: number): boolean =>
  STANDARD_PITCHES_MM.some((pitch) => Math.abs(pitch - pitchMm) <= 1e-3)

const asTap = (op: Operation): TapOperation | null => (op.kind === 'tap' ? op : null)

/**
 * A tap: the machine has to be rigid, the pitch has to be a positive number, every hole has to
 * stand inside the stock. The errors of the layout come from the drilling reader.
 */
export function checkTap(program: ProgramShape & Pick<QuickProgram, 'params'>, op: Operation, out: Problem[]): void {
  const tap = asTap(op)
  if (tap === null) return
  // The machine first: without the feature the cycle must not be written at all.
  if (paramValue(program.params ?? {}, RIGID_TAPPING_PARAM) < 1) {
    add(out, 'rigidTapping', 'Станок без жёсткого нарезания: G84 отменить нельзя, включите параметр или сверлите.')
    return
  }
  if (!(tap.pitchMm > 0)) {
    add(out, 'pitchMm', 'Шаг резьбы должен быть больше нуля: это подача за один оборот.')
    return
  }
  const layout = tapHoles(tap)
  for (const error of layout.errors) add(out, 'geometry', error)
  const { min, max } = program.stock
  const r = tap.tool.diameterMm / 2
  for (const hole of layout.points) {
    if (hole.x - r < min.x - EPS || hole.x + r > max.x + EPS || hole.y - r < min.y - EPS || hole.y + r > max.y + EPS) {
      add(
        out,
        'geometry',
        `Резьба (${fmt(hole.x)}, ${fmt(hole.y)}) вне заготовки: с метчиком ф${fmt(tap.tool.diameterMm)} мм ` +
          `она заходит за края X ${fmt(min.x)}..${fmt(max.x)}, Y ${fmt(min.y)}..${fmt(max.y)} мм.`,
      )
      return
    }
  }
}

/**
 * The tap warnings: what does not block the save but the operator should see. A pitch off the
 * standard series is still a thread, but not a stock one; the form shows it next to the pitch.
 */
export function tapWarnings(op: TapOperation): Problem[] {
  const out: Problem[] = []
  if (op.pitchMm > 0 && !isStandardPitch(op.pitchMm)) {
    add(out, 'pitchMm', `Нестандартный шаг резьбы ${fmt(op.pitchMm)} мм: проверьте.`)
  }
  return out
}
