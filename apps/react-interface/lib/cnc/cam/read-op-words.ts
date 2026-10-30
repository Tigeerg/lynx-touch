// The words of one `op.*` value of a `.qprog` file (PLAN-QUICKPROG, 6, 7.4), C3.
//
// The line loop is in `read-program.ts`, the table of kinds and the dispatcher in `read-op.ts`. This
// module holds the small lexer the shape readers of both `read-op-shapes.ts` (contour, slot, chamfer)
// and `read-op-classic.ts` (drill, face, pocket, circle-pocket) share: a number, the exact arity of
// a key, an `on`/`off` word, and a failure that names the key. It imports no operation type, so the
// two shape modules cannot pull each other.
import { parseNumber, parseOnOff } from './serialize-words'
import { QprogError } from './qprog-error'

/** One numeric value: the field of `words[index]`, or a failure that names the key. */
export const num = (words: string[], index: number, key: string, where: string): number => {
  const value = words[index] === undefined ? null : parseNumber(words[index])
  if (value === null) throw new QprogError(0, `${where}${key} ждёт число.`)
  return value
}

/** The exact arity of a key: `count` values, not more and not fewer. */
export const arity = (words: string[], count: number, key: string, where: string): void => {
  if (words.length !== count + 1) {
    throw new QprogError(
      0,
      `${where}поле '${key}' ждёт ${String(count)} знач., а получило ${String(words.length - 1)}.`,
    )
  }
}

/** The `on`/`off` value of `words[1]`, or a failure that names the key. */
export const onOff = (words: string[], key: string, where: string): boolean => {
  const value = words.length === 2 ? parseOnOff(words[1]) : null
  if (value === null) throw new QprogError(0, `${where}${key} ждёт 'on' или 'off'.`)
  return value
}
