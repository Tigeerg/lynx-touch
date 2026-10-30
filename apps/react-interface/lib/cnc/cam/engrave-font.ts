// The built-in single-stroke font of engraving (`engrave`, PLAN-QUICKPROG 7.6).
//
// A glyph is a list of polylines in the unit square: `y = 0` is the baseline and `y = 1` the cap
// height, so a string of `h` millimetres is the box scaled by `h`. A letter of the set is 1-4
// strokes of 2-6 points: the fewest lines that still read as the letter from above the part. The
// forms are drawn by this project, not taken from a font file: no Hershey table and no outline font
// is shipped or copied, and the points stay coarse on purpose, because a 2 mm mill cannot cut a
// serif. The tables live in `engrave-digits.ts`, `engrave-latin.ts` and `engrave-cyrillic.ts`; this
// module joins them and names the set. A character that is not in the table is an error of the
// form, never a silent box.
//
// The table is keyed by the character, `' '` is the empty list; `SUPPORTED` is the explicit list the
// form and the check name, so nothing is supported by accident and nothing is hidden.
import type { Vec2 } from './contour-offset'
import { DIGITS } from './engrave-digits'
import { LATIN } from './engrave-latin'
import { CYRILLIC } from './engrave-cyrillic'

/** One stroke: a polyline of unit-square points, the first point and the last being its ends. */
export type Stroke = Vec2[]

/** The whole table: the digits, the Latin capitals, the Cyrillic capitals and the space. */
export const GLYPHS: Record<string, Stroke[]> = {
  ' ': [],
  ...DIGITS,
  ...LATIN,
  ...CYRILLIC,
}

/** The characters the check accepts, in a fixed order: the table is the only source of the set. */
export const SUPPORTED: readonly string[] = [
  ...' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ',
  ...'АБВГДЕЖЗИКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ',
].filter((ch) => GLYPHS[ch] !== undefined)

/** The glyph of a character, or `null` when the set does not have it. */
export const glyphOf = (char: string): Stroke[] | null => GLYPHS[char] ?? null

/** True when every character of the text is in the set. */
export const supportedText = (text: string): boolean => [...text].every((ch) => GLYPHS[ch] !== undefined)
