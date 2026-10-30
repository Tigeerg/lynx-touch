// The engraving planner (PLAN-QUICKPROG 7.6): the string laid out by the built-in font.
//
// The layout is measured in the unit square of `engrave-font.ts`: a character is its glyph strokes
// scaled by the height, the next character starts `CHAR_STEP * height` to the right, and the whole
// string is centred on the X of the operation. The turn `angleDeg` rotates the laid-out string around
// the operation point (counter-clockwise, the usual maths sense), so a turned label still hangs off
// the point the operator typed.
//
// One pass: a rapid at safe Z over the first point of a stroke, a rapid down to the top, a plunge to
// `topZ - depthMm`, the stroke at the working feed, then a cut back up to the top. The string is laid
// out once per operation and cut at that one plane: a character's strokes are drawn top to bottom in
// the font, and `depthMm` is the one depth of the label (the step-down field is zero in the form).
import type { EngraveOperation, Operation, Point3, QuickProgram } from './ops'
import { glyphOf, supportedText } from './engrave-font'
import type { Stroke } from './engrave-font'
import type { Vec2 } from './contour-offset'
import { PathBuilder } from './path-builder'

/** The advance of one character, share of the height: the glyph box and the gap between letters. */
export const CHAR_STEP = 0.6

/** The width of one glyph box, share of the height (the unit square of the font). */
const BOX_MM = 1

/** The string laid out flat: the strokes in machine XY, the bounds and the width. */
export interface EngraveLayout {
  strokes: Stroke[]
  /** The left edge and the right edge of the string along the text axis, mm. */
  xMin: number
  xMax: number
  /** `xMax - xMin`: the width of the string, mm. */
  widthMm: number
}

const engrave = (op: Operation): EngraveOperation => {
  if (op.kind !== 'engrave') throw new Error(`planEngrave called with a ${op.kind} operation`)
  return op
}

/** The first character of the text outside the set, or `null` when every one of them is drawn. */
export const unsupportedChar = (text: string): string | null => {
  for (const char of text) if (glyphOf(char) === null) return char
  return null
}

/** The point of the unit square of a glyph on the part, before the turn, mm. */
const unitPoint = (op: EngraveOperation, charAt: number, point: Vec2): Vec2 => ({
  x: op.x + (charAt * CHAR_STEP + point.x) * op.heightMm,
  y: op.y + point.y * op.heightMm,
})

/** The laid-out string: every stroke of the text in machine XY, left to right, centred on `x`. */
export function engraveLayout(operation: EngraveOperation): EngraveLayout {
  const op = engrave(operation)
  const strokes: Stroke[] = []
  const chars = [...op.text]
  // A glyph box is one unit wide, so `n - 1` advances plus one box: the last gap is not part of the
  // string and the centre sits where the operator expects it.
  const steps = Math.max(chars.length - 1, 0)
  const widthMm = chars.length === 0 ? 0 : steps * 0.6 + 1
  const origin = op.x - (widthMm / 2) * op.heightMm
  const laid = { ...op, x: origin }
  chars.forEach((char, at) => {
    for (const stroke of glyphOf(char) ?? []) strokes.push(stroke.map((point) => unitPoint(laid, at, point)))
  })
  const xMin = strokes.length === 0 ? origin : Math.min(...strokes.flat().map((point) => point.x))
  const xMax = strokes.length === 0 ? origin : Math.max(...strokes.flat().map((point) => point.x))
  return { strokes: turn(strokes, op), xMin, xMax, widthMm }
}

/**
 * The strokes turned by `angleDeg` around the operation point. The card reads the width of the
 * flat string, so the bounds of the layout are measured before the turn.
 */
const turn = (strokes: Stroke[], op: EngraveOperation): Stroke[] => {
  const rad = (op.angleDeg * Math.PI) / 180
  const c = Math.cos(rad)
  const s = Math.sin(rad)
  return strokes.map((stroke) =>
    stroke.map((point) => {
      const dx = point.x - op.x
      const dy = point.y - op.y
      return { x: op.x + dx * c - dy * s, y: op.y + dx * s + dy * c }
    }),
  )
}

/** The engraving of one operation, appended to `out`; a text outside the set plans nothing. */
export function planEngrave(program: QuickProgram, operation: Operation, out: PathBuilder): EngraveLayout {
  const op = engrave(operation)
  const layout = engraveLayout(op)
  if (op.heightMm <= 0 || !supportedText(op.text)) return layout
  const top = op.topZ
  const cutZ = top - op.depthMm
  const at = (point: Vec2, z: number): Point3 => ({ x: point.x, y: point.y, z })
  layout.strokes.forEach((stroke) => {
    const first = stroke[0]
    const last = stroke[stroke.length - 1]
    if (first === undefined || last === undefined) return
    // Every stroke comes from above at safe Z: a stroke may start over a cut the previous one left,
    // so a slide along the top would drag the tool through the part between two letters.
    out.add('rapid', at(first, program.safeZ))
    out.add('rapid', at(first, top))
    out.add('cut', at(first, cutZ), op.cutting.plungeFeedMmMin)
    for (let i = 1; i < stroke.length; i += 1) {
      out.add('cut', at(stroke[i], cutZ), op.cutting.feedMmMin)
    }
    out.add('cut', at(last, top), op.cutting.plungeFeedMmMin)
  })
  return layout
}
