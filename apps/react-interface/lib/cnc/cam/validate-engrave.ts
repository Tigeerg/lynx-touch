// The rules of engraving (PLAN-QUICKPROG 7.6): the text, the height and the mill against the lines.
//
// The text is the operation: empty text engraves nothing and a character outside the built-in set
// has no line to cut, so both are refused, the second by naming the character. A height above zero
// scales the glyph table; under a millimetre the cut stays legal but the operator should know it is
// barely readable (`pathWarnings` of `validate-paths.ts`). A line of one stroke is as thick as the mill: a mill wider
// than `THINNEST_GAP * height` fills the counters of a letter, which is a warning as well.
import { fmt } from './feeds'
import type { EngraveOperation, Operation } from './ops'
import { unsupportedChar } from './path-engrave'
import type { Problem } from './validate-common'
import { EPS, add } from './validate-common'

/** A letter of a stroke thinner than the mill loses its counters: the smallest gap of the font. */
const THINNEST_GAP = 0.3

/** The engraving rules: the text is a non-empty string of the built-in set, the height is positive. */
export function checkEngrave(op: Operation, out: Problem[]): void {
  if (op.kind !== 'engrave') return
  const engrave: EngraveOperation = op
  if (engrave.text.length === 0) {
    add(out, 'text', 'Пустой текст: введите надпись, которую нужно выгравировать.')
    return
  }
  const bad = unsupportedChar(engrave.text)
  if (bad !== null) {
    add(out, 'text', `Символ «${bad}» не поддержан.`)
    return
  }
  if (!(engrave.heightMm > 0)) {
    add(out, 'heightMm', 'Высота надписи должна быть больше нуля.')
  }
}

/** The warnings of the height: a text under a millimetre reads only under a magnifier. */
export function engraveToolWarnings(op: EngraveOperation): Problem[] {
  const out: Problem[] = []
  const thinnest = op.heightMm * THINNEST_GAP
  if (op.heightMm > 0 && op.tool.diameterMm > thinnest + EPS) {
    add(
      out,
      'tool.diameterMm',
      `Фреза ф${fmt(op.tool.diameterMm)} мм шире просвета буквы ${fmt(thinnest)} мм: ` +
        'мелкие перемычки зальются, возьмите фрезу тоньше.',
    )
  }
  return out
}
