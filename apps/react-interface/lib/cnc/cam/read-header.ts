// The document header of a `.qprog` file (PLAN-QUICKPROG, 6, 7.4), C3.
//
// One branch per header key: the arity it takes and the field it fills. The schema is checked by the
// line loop; the writer of `serialize-write.ts` writes every default out, so this reader only
// falls back on the optional fields of PLAN-QUICKPROG, 7.3 (`g64P`, `g64Q`, `stopAfterOp`) that
// a file of the previous build does not carry. `where` is the `строка N: ` prefix of a message.
import type { QuickProgram } from './ops'
import { parseNumber, unquoteText, restAfter, WCS_WORDS } from './serialize-words'
import { QprogError } from './qprog-error'

/** The header keys read so far; the duplicate check and the required check both read it. */
export interface Header {
  seen: string[]
}

const fail = (where: string, reason: string): never => {
  throw new QprogError(0, where + reason)
}

/** The keys every operation writes, in the demo order; `notes` is optional. */
export const COMMON_KEYS = [
  'enabled',
  'tool',
  'spindle',
  'feed',
  'plunge',
  'coolant',
  'topZ',
  'depth',
  'stepDown',
  'notes',
]

const REQUIRED_HEADER = ['schema', 'name', 'units', 'wcs', 'stock', 'safeZ']

/** Fills one header field; the schema itself is checked by the line loop. */
export function readHeaderKey(program: QuickProgram, key: string, line: string, words: string[], where: string): void {
  const one = (): string => {
    if (words.length !== 2) fail(where, `${key} ждёт одно значение.`)
    return words[1]
  }
  if (key === 'schema') {
    return
  }
  if (key === 'name') {
    program.name = unquoteText(restAfter(line, 1))
    return
  }
  if (key === 'units') {
    // The panel writes and reads millimetres only; another word would be a silent unit mismatch.
    if (one() !== 'mm') fail(where, "units ждёт 'mm'.")
    return
  }
  if (key === 'wcs') {
    const value = one()
    if (!(WCS_WORDS as string[]).includes(value)) fail(where, `wcs ждёт ${WCS_WORDS.join(', ')}.`)
    program.wcs = value as QuickProgram['wcs']
    return
  }
  if (key === 'stock') {
    const values = words.slice(1)
    const parsed = values.length === 6 ? values.map(parseNumber) : [null]
    if (parsed.some((value) => value === null)) fail(where, 'stock ждёт шесть чисел: xmin ymin zmin xmax ymax zmax.')
    const [xMin, yMin, zMin, xMax, yMax, zMax] = parsed as number[]
    program.stock = { min: { x: xMin, y: yMin, z: zMin }, max: { x: xMax, y: yMax, z: zMax } }
    return
  }
  if (key === 'safeZ') {
    const value = words.length === 2 ? parseNumber(words[1]) : null
    if (value === null) fail(where, 'safeZ ждёт число.')
    program.safeZ = value as number
    return
  }
  if (key === 'g64P' || key === 'g64Q') {
    // Optional: a file of the previous build has no tolerance and is read with 0.01 (7.3). A bare
    // key is the console's "use the default", the same absence.
    if (words.length === 1) return
    const value = words.length === 2 ? parseNumber(words[1]) : null
    if (value === null) fail(where, `${key} ждёт число.`)
    if (key === 'g64P') program.g64P = value as number
    else program.g64Q = value as number
    return
  }
  if (key === 'stopAfterOp') {
    const off = words.length === 2 && words[1] === 'off'
    const on = words.length === 2 && words[1] === 'on'
    if (!on && !off) fail(where, "stopAfterOp ждёт 'on' или 'off'.")
    program.stopAfterOp = on
    return
  }
  fail(where, `неизвестное поле '${key}'.`)
}

/** Checks the required header keys one by one, so the message names the field, not "broken file". */
export function headerComplete(header: Header): void {
  for (const key of REQUIRED_HEADER) {
    if (!header.seen.includes(key)) throw new QprogError(1, `нет обязательного поля '${key}'.`)
  }
}
