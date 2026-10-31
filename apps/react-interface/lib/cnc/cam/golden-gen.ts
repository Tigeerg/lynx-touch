// The writer of the Q6 golden pair (PLAN-QUICKPROG, §9 Q6): `demo.qprog` and `demo.ngc`.
//
// The panel is the source of both files: the document comes from `golden-doc.ts`, the `.qprog` from
// `serializeProgram` and the `.ngc` from the plannable document through `emitProgram`, so the pair
// is exactly what the panel saves. The module writes and does not decide: the checks (planning, the
// viewer rules, the collision check, the counts) live in `golden.test.ts`, and this is called only
// by the small runner the README describes (`scripts/golden-q6-gen.mjs` through esbuild, or a
// one-line `node -e`). It is not imported by the application.
import { writeFileSync } from 'node:fs'
import { emitProgram } from './emit'
import { goldenProgram } from './golden-doc'
import { buildProgram } from './path'
import { serializeProgram } from './serialize'

/** The planned document without the machine parameters: a `.qprog` is the part, not the machine. */
const partOf = (program: ReturnType<typeof goldenProgram>): ReturnType<typeof goldenProgram> => {
  const { params: _machine, ...part } = program
  void _machine
  return part
}

/**
 * Writes `demo.qprog` and `demo.ngc` into `dir`; returns the number of lines of the program. The
 * document is written first, then read back by the caller's test, so a reader that disagrees with
 * the writer fails there and not in review.
 */
export function writeGolden(dir: string): { qprogPath: string; ngcPath: string; lines: number } {
  const program = goldenProgram()
  const built = buildProgram(program)
  if (!built.ok) throw new Error(`the golden document does not plan: ${built.problems.join('; ')}`)
  const lines = emitProgram(program, built.path)
  const qprogPath = `${dir}/demo.qprog`
  const ngcPath = `${dir}/demo.ngc`
  writeFileSync(qprogPath, serializeProgram(partOf(program)))
  writeFileSync(ngcPath, `${lines.join('\n')}\n`)
  return { qprogPath, ngcPath, lines: lines.length }
}
