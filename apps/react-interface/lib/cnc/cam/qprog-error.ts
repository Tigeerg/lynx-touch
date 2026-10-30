// A bad line of a `.qprog` file (PLAN-QUICKPROG, 7.4), C3.
//
// The reader throws this from the depth of one field; `readProgram` turns it into
// `{ ok: false, line, reason }`. The reason is Russian with the field spelled as in the file.
export class QprogError extends Error {
  constructor(
    readonly line: number,
    readonly reason: string,
  ) {
    super(reason)
    this.name = 'QprogError'
  }
}

/** A nested failure keeps its own line when it has one; otherwise the calling line is the fault. */
export const withLine = (error: unknown, line: number): never => {
  if (error instanceof QprogError) throw new QprogError(error.line > 0 ? error.line : line, error.reason)
  throw error
}
