// The ladder comparison parse: the operand grammar the editor accepts in a comparison block and
// its text form for the label.
import { CMP_RE, isOperand } from './ladder-data'
import type { CompareOp, LadderCompare } from './types'

export function parseCompare(text: string): { op: CompareOp; a: string; b: string } | null {
  const m = CMP_RE.exec(text)
  if (!m) return null
  const [, a, op, b] = m
  if (!isOperand(a) || !isOperand(b)) return null
  return { op: op as CompareOp, a, b }
}

export const compareText = (c?: LadderCompare) => (c ? `${c.a} ${c.op} ${c.b}` : '')
