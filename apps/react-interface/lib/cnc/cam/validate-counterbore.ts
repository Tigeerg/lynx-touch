// The counterboring and thread-milling rules (PLAN-QUICKPROG 7.6): the head of a fastener and the
// body of a mill against a thread.
//
// Counterbore: the seat has to be at least the hole (a narrower seat is not a seat), its depth has
// to be positive (a zero seat is no seat) and not finer than its own diameter: a seat finer than its
// width takes the head of the fastener with a sliver, which is a warning, not a refused save. The R
// plane is required by the note: a hole without R rapid-descends from safe Z into the material, so a
// missing or low R is refused.
//
// Thread: the pitch is positive and not coarser than the mill, or the helix step is wider than the
// tool. An internal thread needs the mill inside the hole: `D/2 < r - pitch/2`, else the mill body
// eats the flank. An external one needs the mill thinner than the valley: `D <= thread diameter -
// pitch`, else the flanks cannot be cut. Both numbers are the note's simplified checks.
import { fmt } from './feeds'
import type { CounterboreOperation, Operation, ProgramShape, ThreadOperation } from './ops'
import type { Problem } from './validate-common'
import { EPS, add } from './validate-common'
import { counterboreHoles } from './tap-holes'

/** The default dwell of G82 when the document does not set it, seconds. */
export const DEFAULT_DWELL_SEC = 0.2

/** A counterbore's own fields, or null for another kind. */
const asCounterbore = (op: Operation): CounterboreOperation | null => (op.kind === 'counterbore' ? op : null)

/** The hole body inside the stock, the same rule a drill states; the first hole outside is named. */
function checkHoleBody(
  program: ProgramShape,
  points: { x: number; y: number }[],
  toolMm: number,
  word: string,
  out: Problem[],
): boolean {
  const { min, max } = program.stock
  const r = toolMm / 2
  for (const hole of points) {
    if (hole.x - r < min.x - EPS || hole.x + r > max.x + EPS || hole.y - r < min.y - EPS || hole.y + r > max.y + EPS) {
      add(
        out,
        'geometry',
        `${word} (${fmt(hole.x)}, ${fmt(hole.y)}) вне заготовки: с инструментом ф${fmt(toolMm)} мм ` +
          `она заходит за края X ${fmt(min.x)}..${fmt(max.x)}, Y ${fmt(min.y)}..${fmt(max.y)} мм.`,
      )
      return false
    }
  }
  return true
}

/** A counterbore: the seat width and depth, the R plane and every seat inside the stock. */
export function checkCounterbore(program: ProgramShape, op: Operation, out: Problem[]): void {
  const cb = asCounterbore(op)
  if (cb === null) {
    // A thread has its own module; the switch in `validate-shape.ts` routes it.
    checkThread(program, op, out)
    return
  }
  const holeDiameter = cb.hole.r * 2
  if (!(cb.seatDiameterMm > 0)) {
    add(out, 'seatDiameterMm', 'Диаметр цековки должен быть больше нуля.')
    return
  }
  if (cb.seatDiameterMm < holeDiameter - EPS) {
    add(out, 'seatDiameterMm', 'Цековка уже отверстия')
    return
  }
  if (!(cb.seatDepthMm > 0)) {
    add(out, 'seatDepthMm', 'Глубина цековки должна быть больше нуля.')
    return
  }
  if (!(cb.dwellSec >= 0)) {
    add(out, 'dwellSec', 'Выдержка цековки не может быть отрицательной.')
  }
  if (!cb.hasR) {
    add(out, 'rMm', 'Задайте плоскость R цековки: без неё цикл возьмёт высоту по умолчанию.')
    return
  }
  if (cb.rMm < cb.topZ - EPS) {
    add(out, 'rMm', `R ${fmt(cb.rMm)} мм ниже верха материала ${fmt(cb.topZ)} мм: быстрый ход войдёт в металл.`)
    return
  }
  const layout = counterboreHoles(cb)
  for (const error of layout.errors) add(out, 'geometry', error)
  checkHoleBody(program, layout.points, Math.max(cb.tool.diameterMm, cb.seatDiameterMm), 'Цековка', out)
}

/** The counterbore warnings: a shallow seat over a wide head is still a seat, but a weak one. */
export function counterboreWarnings(op: CounterboreOperation): Problem[] {
  if (!(op.seatDepthMm > 0) || !(op.seatDiameterMm > 0)) return []
  if (op.seatDepthMm < op.seatDiameterMm - EPS) {
    return [{ field: 'seatDepthMm', reason: 'Цековка мельче диаметра: проверьте крепёж' }]
  }
  return []
}

/** A thread: the pitch against the mill, and the body of the mill against the thread profile. */
function checkThread(program: ProgramShape, op: Operation, out: Problem[]): void {
  if (op.kind !== 'thread') return
  const thread: ThreadOperation = op
  if (!(thread.hole.r > 0)) {
    add(out, 'hole.r', 'Радиус резьбы должен быть больше нуля.')
    return
  }
  if (!(thread.pitchMm > 0)) {
    add(out, 'pitchMm', 'Шаг резьбы должен быть больше нуля: это подъём спирали за оборот.')
    return
  }
  const d = thread.tool.diameterMm
  if (thread.pitchMm > d + EPS) {
    add(out, 'pitchMm', `Шаг ${fmt(thread.pitchMm)} мм больше фрезы ${fmt(d)} мм: спираль шире инструмента.`)
    return
  }
  if (!(thread.threadDepthMm > 0)) {
    add(out, 'threadDepthMm', 'Длина резьбы должна быть больше нуля.')
    return
  }
  if (thread.internal) {
    if (d / 2 >= thread.hole.r - thread.pitchMm / 2 - EPS) {
      add(out, 'tool.diameterMm', 'Фреза не проходит в отверстие под резьбу')
      return
    }
  } else if (d > thread.hole.r * 2 - thread.pitchMm + EPS) {
    add(out, 'tool.diameterMm', 'Фреза не тоньше впадины резьбы: возьмите фрезу поменьше.')
    return
  }
  checkHoleBody(program, [{ x: thread.hole.x, y: thread.hole.y }], 2 * thread.hole.r, 'Резьба', out)
}
