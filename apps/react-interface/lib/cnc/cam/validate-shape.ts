// The shape rules of every operation kind (PLAN-QUICKPROG, 8 and 7.4).
//
// Face: the mill fits the stock and the strip overlap is sane. Pocket: the mill body with the wall
// allowance fits inside the contour and the raster step is between 10 and 100 % of the diameter.
// Drilling: every hole with its body stands inside the stock, and the peck Q is a real step down
// the hole. Contour: the profile has two points and every corner is open enough for the mill body
// (`validate-contour.ts`). Slot and chamfer: the two ends make a line, the mill fits the slot width,
// the chamfer leg does not exceed the mill radius (`validate-slot.ts`). Tapping reads the machine
// switch and the pitch (`validate-tap.ts`); a custom block has no shape at all
// (`validate-custom.ts`). The reasons are the texts of the console of `apps/quickprog`, so the panel
// and the demo say the same words.
import { drillHoles } from './drill-holes'
import { fmt } from './feeds'
import type {
  CirclePocketOperation,
  DrillOperation,
  FaceOperation,
  Operation,
  ProgramShape,
  RectPocketOperation,
} from './ops'
import type { Problem } from './validate-common'
import { EPS, MIN_PECK_MM, MIN_STEP_MM, add } from './validate-common'
import { checkContour } from './validate-contour'
import { checkChamfer, checkSlot } from './validate-slot'
import { checkTap } from './validate-tap'
import { checkCustom } from './validate-custom'

/** The shape rules of the kind of the operation: face, pocket, circle pocket, drilling, contour, slot, chamfer. */
export function checkGeometry(
  program: ProgramShape & { params?: Record<string, number> },
  op: Operation,
  out: Problem[],
): void {
  // The narrow field by the kind: the union makes a forgotten kind a compile error.
  switch (op.kind) {
    case 'face':
      return checkFace(program, op, out)
    case 'pocket':
      return checkPocket(op, out)
    case 'circle-pocket':
      return checkCirclePocket(op, out)
    case 'drill':
      return checkDrill(program, op, out)
    case 'contour':
      return checkContour(program, op, out)
    case 'slot':
      return checkSlot(op, out)
    case 'chamfer':
      return checkChamfer(op, out)
    case 'tap':
      return checkTap(program, op, out)
    case 'custom':
      return checkCustom(op, out)
  }
}

/** The raster step of a pocket: under 10 % the mill rubs, over 100 % a strip stays uncut. */
function checkStepOver(pct: number, out: Problem[]): void {
  if (pct < 10 - EPS) {
    add(out, 'stepOver', `Шаг ${fmt(pct)} % меньше 10 % диаметра: фреза будет тереть уже срезанное.`)
  } else if (pct > 100 + EPS) {
    add(out, 'stepOver', `Шаг ${fmt(pct)} % больше 100 % диаметра: между проходами останутся дыры.`)
  }
}

/** Face: the mill fits the stock width, the strip overlap is sane, the plane is not above the top. */
function checkFace(program: ProgramShape, op: FaceOperation, out: Problem[]): void {
  const widthY = program.stock.max.y - program.stock.min.y
  const lengthX = program.stock.max.x - program.stock.min.x
  if (widthY <= 0 || lengthX <= 0) {
    add(out, 'geometry', 'Заготовка вырождена: ширина или длина не больше нуля.')
    return
  }
  if (op.tool.diameterMm > widthY) {
    add(
      out,
      'geometry',
      `Фреза ${fmt(op.tool.diameterMm)} мм шире заготовки ${fmt(widthY)} мм: возьмите фрезу поменьше.`,
    )
  }
  if (op.stepOverPct < 20 || op.stepOverPct > 90) {
    add(
      out,
      'geometry',
      `Перекрытие полос ${fmt(op.stepOverPct)} % вне разумных 20..90 %: полосы либо не сомкнутся, либо прожгут.`,
    )
  }
  if (op.z > op.topZ + EPS) {
    add(out, 'geometry', 'Плоскость обработки выше верха материала.')
  }
  if (op.depthMm > 0 && op.stepDownMm > 0 && op.stepDownMm < MIN_STEP_MM) {
    add(out, 'stepDownMm', `Шаг врезания ${fmt(op.stepDownMm)} мм меньше 0.1 мм: проходов будет очень много.`)
  }
}

/** Pocket: the mill body with the allowance fits inside the contour, walls and corner radius. */
function checkPocket(op: RectPocketOperation, out: Problem[]): void {
  // The first broken field wins: fix the allowance before asking whether the mill fits.
  const { rect, finishAllowanceMm: allowance, cornerRadiusMm: corner, tool } = op
  if (rect.w <= 0 || rect.h <= 0) {
    add(out, 'geometry', 'Карман вырожден: ширина или высота не больше нуля.')
    return
  }
  if (allowance < 0) {
    add(out, 'geometry', `Припуск ${fmt(allowance)} мм отрицательный: траектория выйдет за контур кармана.`)
    return
  }
  if (corner < 0) {
    add(out, 'geometry', `Радиус угла ${fmt(corner)} мм не может быть отрицательным.`)
    return
  }
  checkStepOver(op.stepOverPct, out)
  const shortSide = Math.min(rect.w, rect.h)
  if (corner > shortSide / 2 + EPS) {
    add(out, 'geometry', `Радиус угла ${fmt(corner)} мм больше половины короткой стороны: такого кармана нет.`)
    return
  }
  const inset = tool.diameterMm / 2 + allowance
  if (inset + EPS >= shortSide / 2) {
    add(
      out,
      'geometry',
      `Фреза ф${fmt(tool.diameterMm)} мм и припуск ${fmt(allowance)} мм не влезают в карман ${fmt(rect.w)}x` +
        `${fmt(rect.h)} мм: нужно не больше ${fmt(shortSide / 2)} мм от стенки, а получается ${fmt(inset)} мм. ` +
        'Возьмите фрезу поменьше.',
    )
  }
}

/** Circle pocket: the mill body with the allowance fits inside the pocket radius, strictly. */
function checkCirclePocket(op: CirclePocketOperation, out: Problem[]): void {
  const { circle, finishAllowanceMm: allowance, tool } = op
  if (circle.r <= 0) {
    add(out, 'geometry', `Круглый карман вырожден: радиус ${fmt(circle.r)} мм не больше нуля.`)
    return
  }
  if (allowance < 0) {
    add(out, 'geometry', `Припуск ${fmt(allowance)} мм отрицательный: траектория выйдет за контур кармана.`)
    return
  }
  checkStepOver(op.stepOverPct, out)
  const inset = tool.diameterMm / 2 + allowance
  if (inset + EPS >= circle.r) {
    add(
      out,
      'geometry',
      `Фреза ф${fmt(tool.diameterMm)} мм и припуск ${fmt(allowance)} мм не влезают в круглый карман ф` +
        `${fmt(2 * circle.r)} мм: от центра до стенки ${fmt(circle.r)} мм, а фреза с припуском занимает ` +
        `${fmt(inset)} мм. Возьмите фрезу поменьше.`,
    )
  }
}

const isDrillOperation = (op: Operation): op is DrillOperation => op.kind === 'drill'

/** A hole with its body has to stand inside the stock; only the first hole outside is named. */
function checkDrill(program: ProgramShape, op: Operation, out: Problem[]): void {
  if (!isDrillOperation(op)) return
  const { points, errors } = drillHoles(op)
  for (const error of errors) add(out, 'geometry', error)
  const { min, max } = program.stock
  const r = op.tool.diameterMm / 2
  for (const hole of points) {
    if (hole.x - r < min.x - EPS || hole.x + r > max.x + EPS || hole.y - r < min.y - EPS || hole.y + r > max.y + EPS) {
      add(
        out,
        'geometry',
        `Отверстие (${fmt(hole.x)}, ${fmt(hole.y)}) вне заготовки: с инструментом ф${fmt(op.tool.diameterMm)} мм ` +
          `оно заходит за края X ${fmt(min.x)}..${fmt(max.x)}, Y ${fmt(min.y)}..${fmt(max.y)} мм. ` +
          'Сдвиньте отверстие или уменьшите фрезу.',
      )
      return
    }
  }
}

/** The peck Q of G83 is a step down the hole: not deeper than the hole and not a speck. */
export function checkPeck(op: Operation, out: Problem[]): void {
  if (!isDrillOperation(op)) return
  const peck = op.peckMm
  if (peck <= 0) return
  if (op.depthMm > 0 && peck > op.depthMm + EPS) {
    add(
      out,
      'peck',
      `Шаг вывода стружки Q${fmt(peck)} мм больше глубины отверстия ${fmt(op.depthMm)} мм: такого цикла нет.`,
    )
  } else if (peck < MIN_PECK_MM - EPS) {
    add(out, 'peck', `Шаг вывода стружки Q${fmt(peck)} мм меньше 0.1 мм: цикл будет только дробить стружку.`)
  }
}
