// The spelling of the emitted file (PLAN-QUICKPROG, 7.1): blocks, coordinates, tags (C2).
//
// One place keeps the block number, the coordinate and the tag text of the whole file, so the
// header, the moves and the cycles cannot drift apart and the golden files of `etl/` compare the
// same spelling everywhere. The text repeats the demo's `emit_internal.h`, character for character.
import type { Operation } from './ops'

/** The step of the block numbers, the one of the reference post file. */
export const SEQUENCE_STEP = 5

/** One numbered block: `N100 G1 X1.000`; the caller advances the sequence by `SEQUENCE_STEP`. */
export const numberedBlock = (sequence: number, words: string): string => `N${sequence} ${words}`

/** A coordinate of the file: three digits after the decimal point, always. */
export const coord = (value: number): string => {
  if (!Number.isFinite(value)) throw new Error(`a coordinate of the file is not a number: ${value}`)
  return value.toFixed(3)
}

/** A tag body number: `%g` with a trailing dot (`XMIN=-20.`, `D=10.`), the reference spelling. */
export const num = (value: number): string => {
  const text = String(value)
  const finite = Number.isFinite(value)
  if (text.includes('.') || text.includes('e') || !finite) return text
  return `${text}.`
}

/** A feed word: an integer when the value is whole (`F860`), else up to three digits. */
export const feedWord = (value: number): string =>
  Math.abs(value - Math.round(value)) < 1e-6 ? String(Math.round(value)) : value.toFixed(3)

/** A viewer tag line: `(TAG: ...)`, without a block number, as the reference files write it. */
export const tag = (body: string): string => `(${body})`

/**
 * G-code comments may not carry parentheses: an unbalanced bracket ends the comment early and the
 * rest of the line becomes program words.
 */
export const commentSafe = (text: string): string => text.replace(/[()]/g, ' ').replace(/[\r\n]/g, '')

/** The viewer labels a stage by tool family: FLAT for the milling kinds, DRILL for a hole, TAP for a
 * tap; a free block has no family of its own and is read as CODE. */
export const toolType = (operation: Operation): string =>
  operation.kind === 'drill'
    ? 'DRILL'
    : operation.kind === 'tap'
      ? 'TAP'
      : operation.kind === 'custom'
        ? 'CODE'
        : 'FLAT'

/** The row of the G43 table of a tool: the explicit number, else the tool number. Never zero. */
export const effectiveH = (operation: Operation): number =>
  operation.tool.hNumber > 0 ? operation.tool.hNumber : operation.tool.number
