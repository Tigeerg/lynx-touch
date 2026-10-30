// The words of a `.qprog` line, shared by the writer and the reader (PLAN-QUICKPROG, 7.4), C3.
//
// The port of `serialize_shared.cpp`: one spelling of a number, of a quote and of a word, so a token
// the writer writes is a token the reader accepts. A change here changes both sides at once.
//
//   * one field per line, `key value ...`, separated by spaces or tabs;
//   * `#` starts a comment line; empty lines are skipped; a UTF-8 byte order mark is dropped;
//   * free text (a name, an id, a note) is the rest of the line and may carry spaces; the writer
//     puts it in double quotes and the reader drops one outer pair, nothing is escaped;
//   * numbers are a full token with a decimal point; `1x` and `1,5` are errors, not 1.
import type { Coolant, OperationKind, Wcs } from './ops'

/** The words of the operation kinds on the `op` line, as the console names them. */
export const KIND_WORDS: Record<OperationKind, string> = {
  face: 'face',
  pocket: 'pocket',
  'circle-pocket': 'circle',
  drill: 'drill',
  contour: 'contour',
}

/** The three geometry words of a drilling operation: one hole, a grid, a ring. */
export type DrillWord = 'drill' | 'drill-grid' | 'drill-circle'

/** The one spelling of a number: 15 significant digits, `-0` written as `0`, like `%.15g`. */
export function qprogNumber(value: number): string {
  if (!Number.isFinite(value)) throw new Error(`the .qprog writer has no spelling for ${String(value)}`)
  const text = String(Number(value.toPrecision(15)))
  return Object.is(Number(text), -0) ? '0' : text
}

/** A full-token number of the file; `NaN` and `Infinity` are not numbers of the format. */
export function parseNumber(text: string): number | null {
  if (!/^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(text)) return null
  const value = Number(text)
  return Number.isFinite(value) ? value : null
}

/** A full-token integer of the file, inside the range the console keeps. */
export function parseInteger(text: string): number | null {
  if (!/^[+-]?\d+$/.test(text)) return null
  const value = Number(text)
  if (!Number.isSafeInteger(value) || value < -2147483647 || value > 2147483647) return null
  return value
}

/** The words of a line: a run of non-blank characters, empty words dropped. */
export const splitWords = (line: string): string[] => line.split(/[ \t]+/).filter((word) => word.length > 0)

/** The rest of the line after `count` words, trailing blanks dropped: an id, a name, a note. */
export function restAfter(line: string, count: number): string {
  let at = 0
  for (let n = 0; n < count; n += 1) {
    while (at < line.length && (line[at] === ' ' || line[at] === '\t')) at += 1
    while (at < line.length && line[at] !== ' ' && line[at] !== '\t') at += 1
  }
  while (at < line.length && (line[at] === ' ' || line[at] === '\t')) at += 1
  return line.slice(at).replace(/[ \t\r]+$/, '')
}

/** The writer's text spelling: quoted, so leading and trailing spaces survive. */
export const quoteText = (text: string): string => `"${text}"`

/** The reader's side of `quoteText`: one outer pair of quotes is dropped. */
export const unquoteText = (text: string): string =>
  text.length >= 2 && text.startsWith('"') && text.endsWith('"') ? text.slice(1, -1) : text

export const COOLANT_WORDS: Coolant[] = ['off', 'mist', 'flood']

export const WCS_WORDS: Wcs[] = ['G54', 'G55', 'G56', 'G57', 'G58', 'G59']

/** The on/off words of a boolean field of the file. */
export const onOff = (value: boolean): string => (value ? 'on' : 'off')

/** A boolean field of the file, or `null` for a word that is neither `on` nor `off`. */
export const parseOnOff = (text: string): boolean | null => (text === 'on' ? true : text === 'off' ? false : null)
