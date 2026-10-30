// The multi-line text of a custom block (PLAN-QUICKPROG 7.6): one program line per form line.
//
// The form holds strings, and a free block is a list: `gcode` of the operation is the list and the
// form field is its text. The two readers live here, so the form of the panel and the tables of
// `components/cnc/cam/cam-fields.ts` split the text the same way, and a save round-trip keeps every
// typed line. A line break is `\n`; `\r\n` of a paste is normalised here.
import type { CustomOperation } from './ops'

/** The text of a block as the form field shows it: one line of the program per line of text. */
export const linesText = (lines: string[]): string => lines.join('\n')

/** The list of lines of the text, with the `\r` of a pasted `\r\n` dropped; an empty text is no lines. */
export const linesOf = (text: string): string[] =>
  text === '' ? [] : text.split('\n').map((line) => line.replace(/\r$/, ''))

/** The block with the typed text applied: the field `gcode` is the list, the identity when untouched. */
export const customWithText = (op: CustomOperation, text: string | undefined): CustomOperation =>
  text === undefined ? op : { ...op, gcode: linesOf(text) }
