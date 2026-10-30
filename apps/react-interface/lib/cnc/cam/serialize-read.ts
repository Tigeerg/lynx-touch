// The `.qprog` reader (PLAN-QUICKPROG, 6, 7.4), C3, the port of `serialize_read*.cpp`.
//
// Strict on purpose: a generated file is edited by hand between two sessions, and a typo must be
// named, not guessed away. An unknown key, a repeated key, a missing required key, a number that
// does not parse and a foreign schema are errors with the line number or the field name, never a
// silent default. A file of the previous build is the one exception: it has no `g64P`/`g64Q`, no
// `op.material`, no `op.h`, no `op.drill` and no `op.stepOver` of a pocket, and those fields are
// read with the defaults of PLAN-QUICKPROG, 7.3.
//
// The line loop is here; one `op.*` value is read by `read-op.ts` and one header value by
// `read-header.ts`. Both throw `QprogError`, whose line is filled here when the reader threw with 0.
import type { Operation, QuickProgram } from './ops'
import { newOperation, newProgram } from './defaults'
import { DRILL_KEYS, opHead, readOpValue, SHAPE_KEYS } from './read-op'
import { COMMON_KEYS, headerComplete, readHeaderKey, type Header } from './read-header'
import { QprogError, withLine } from './qprog-error'
import { restAfter, splitWords, unquoteText } from './serialize-words'

/** The outcome of `parseProgram`: the document, or the line and the reason of the first fault. */
export type ParseResult = { ok: true; program: QuickProgram } | { ok: false; line: number; reason: string }

/** The operation being read: its kind, its drill word and the keys already seen. */
interface OpState {
  op: Operation
  head: ReturnType<typeof opHead>
  seen: string[]
}

/** The kind keys of a head: the shape of the kind and the layout keys of a drilling. */
const kindKeys = (head: OpState['head']): string[] =>
  head.kind === 'drill' ? DRILL_KEYS[head.drill as string] : SHAPE_KEYS[head.kind]

/** The required keys: common and kind, except the notes and the pocket step that have a default. */
const requiredKeys = (state: OpState): string[] =>
  [...COMMON_KEYS, ...kindKeys(state.head)].filter(
    (key) => key !== 'notes' && !(key === 'stepOver' && state.op.kind === 'pocket'),
  )

/** Checks that every required key of the operation was read; the message names the first absent. */
const closeOperation = (state: OpState, line: number): void => {
  for (const key of requiredKeys(state)) {
    if (!state.seen.includes(`op.${key}`)) {
      throw new QprogError(line, `в операции '${state.op.id}' нет обязательного поля 'op.${key}'.`)
    }
  }
}

/** The header keys every document must have before its first operation. */
const REQUIRED_HEADER = ['schema', 'name', 'units', 'wcs', 'stock', 'safeZ']

/** An operation of the kind the head names, with the defaults the demo fills before the file. */
const blankOperation = (head: ReturnType<typeof opHead>): Operation => {
  const init = { id: '', topZ: 0 }
  if (head.kind === 'drill') {
    const op = newOperation('drill', init)
    op.pattern = head.drill === 'drill-grid' ? 'grid' : head.drill === 'drill-circle' ? 'circle' : 'hole'
    return op
  }
  return newOperation(head.kind, init)
}

/** Reads the whole document; the first fatal key throws and the caller turns it into a result. */
function readDocument(text: string): QuickProgram {
  const program = newProgram()
  program.operations = []
  let operation: OpState | null = null
  const header: Header = { seen: [] }
  const lines = text.split('\n')

  for (let index = 0; index < lines.length; index += 1) {
    const lineNo = index + 1
    let line = lines[index].replace(/\r$/, '')
    if (lineNo === 1 && line.startsWith('\uFEFF')) line = line.slice(1)
    const words = splitWords(line)
    if (words.length === 0 || words[0].startsWith('#')) continue
    const where = `строка ${String(lineNo)}: `
    const key = words[0]
    const at = (reason: string): never => {
      throw new QprogError(lineNo, reason)
    }
    // A repeated key is a lost track of a hand edit: the file is generated, so the same key twice
    // is an error, not "last wins". The `op` head opens a new operation by design.
    if (key !== 'op') {
      const scope = operation !== null && key.startsWith('op.') ? operation.seen : header.seen
      // `op.gcode` of a custom block is a list: one line per entry, so a repeat is the list growing.
      const listed = key === 'op.gcode' && operation !== null && operation.op.kind === 'custom'
      if (!listed && scope.includes(key)) at(`поле '${key}' повторяется.`)
    }
    if (key === 'op') {
      if (operation !== null) closeOperation(operation, lineNo)
      if (words.length < 2) at("после 'op' нужен вид операции.")
      if (!header.seen.includes('schema')) at("документ начинается не с 'schema 1'.")
      for (const required of REQUIRED_HEADER) {
        if (!header.seen.includes(required)) at(`нет обязательного поля '${required}' (перед операциями).`)
      }
      const head = opHead(words[1], lineNo)
      const op = blankOperation(head)
      op.id = unquoteText(restAfter(line, 2))
      operation = { op, head, seen: [] }
      program.operations.push(op)
      continue
    }
    if (key.startsWith('op.')) {
      if (operation === null) at(`поле '${key}' идёт до первой операции.`)
      const state = operation as OpState
      try {
        readOpValue(state.op, state.head, key, line, words, where)
      } catch (error) {
        throw withLine(error, lineNo)
      }
      state.seen.push(key)
      continue
    }
    header.seen.push(key)
    if (key === 'schema' && words[1] !== '1') {
      at(`схема ${words[1] ?? '?'} не поддерживается: эта версия читает схему 1.`)
    }
    try {
      readHeaderKey(program, key, line, words, where)
    } catch (error) {
      throw withLine(error, lineNo)
    }
  }
  if (operation !== null) closeOperation(operation, lines.length)
  headerComplete(header)
  if (program.operations.length === 0) {
    throw new QprogError(1, "нет ни одной операции: в документе должен быть хотя бы один 'op'.")
  }
  return program
}

/** Reads a schema 1 document; empty lines and `#` comments are skipped, a BOM is dropped. */
export function readProgram(text: string): ParseResult {
  try {
    return { ok: true, program: readDocument(text) }
  } catch (error) {
    if (error instanceof QprogError) return { ok: false, line: error.line, reason: error.reason }
    throw error
  }
}
