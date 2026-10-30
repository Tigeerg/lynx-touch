// The `.qprog` file: the document the panel saves and opens again (PLAN-QUICKPROG, 6, 7.4), C3.
//
// The format is the C++ demo's, not a second one: a file written by `apps/quickprog --save-qprog`
// opens on the panel and a panel file opens in the demo (`quickprog --qprog file.qprog out.ngc`).
// The tests hold that both ways, byte for byte on the demo files. One module is the door:
//
//   serializeProgram(program) -> text           writer, `serialize-write.ts`
//   parseProgram(text)        -> { ok, ... }    reader, `read-program.ts`
import type { QuickProgram } from './ops'
import { writeProgram } from './serialize-write'
import { readProgram, type ParseResult } from './serialize-read'

export type { ParseResult } from './serialize-read'

/** Writes the document of schema 1; deterministic, optional fields at their default are left out. */
export const serializeProgram = (program: QuickProgram): string => writeProgram(program)

/** Reads a schema 1 document, or names the line and the reason of the first fault. */
export const parseProgram = (text: string): ParseResult => readProgram(text)
