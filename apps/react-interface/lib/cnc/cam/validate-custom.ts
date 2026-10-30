// The free G-code block (`custom`, PLAN-QUICKPROG 7.6): the one check on a document shape.
//
// A custom block has no geometry, so no shape rule can look at it: the file it makes is the lines
// the operator typed. What is checked here is only that the list is not empty (a block of nothing
// would print an operation with no words); a comment-only block is a deliberate marker and passes.
// The lines themselves are the viewer parser's business (`viewer-check.ts`), whose errors are bound
// to the field `gcode` of the operation and block the save.
import type { CustomOperation, Operation } from './ops'
import type { Problem } from './validate-common'
import { add } from './validate-common'

/** A custom block: at least one line, and not only blank lines. */
export function checkCustom(op: Operation, out: Problem[]): void {
  const custom: CustomOperation | null = op.kind === 'custom' ? op : null
  if (custom === null) return
  if (custom.gcode.length === 0 || custom.gcode.every((line) => line.trim() === '')) {
    add(out, 'gcode', 'Свой код пуст: введите хотя бы одну строку G-кода.')
  }
}
