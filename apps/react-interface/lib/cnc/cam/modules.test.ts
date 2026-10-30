// The module layout of the CAM core: the size of a file and the graph of `ops.ts`.
//
// A file over the size guide is a sign that a kind of operation and the mechanics of the pipeline
// live together; the two are cut apart into `*-kinds.ts` (and `read-op-shapes.ts` for the reader).
// The split keeps `ops.ts` a leaf of the type graph: `lib/cnc/types.ts` takes it, so a new module
// must not pull a value of the logic behind it. This test reads the files themselves, so it fails
// on the tree where `ops.ts` still holds the arms of the union.
import { readFileSync, readdirSync, statSync } from 'node:fs'
import { join, relative } from 'node:path'
import { describe, expect, it } from 'vitest'

const CAM = join(__dirname)

/** The size guide of a module, lines; a longer file is split or named in `TODO.md`. */
const SIZE_GUIDE = 250

const sources = (): string[] =>
  readdirSync(CAM)
    .filter((name) => name.endsWith('.ts') && !name.endsWith('.test.ts'))
    .map((name) => join(CAM, name))

const lineCount = (file: string): number => readFileSync(file, 'utf8').split('\n').length - 1

/** The specifiers of the local imports and re-exports, and whether the import is type-only. */
function localImports(file: string): { spec: string; typeOnly: boolean }[] {
  const text = readFileSync(file, 'utf8')
  const found: { spec: string; typeOnly: boolean }[] = []
  // The clause may wrap over lines, so the match spans them up to the `from` word.
  const pattern = /^\s*(import|export)\s+(type\s+)?[\s\S]*?\sfrom\s+['"](\.[^'"]*)['"]/gm
  for (const match of text.matchAll(pattern)) {
    const named = /^\s*(import|export)\s+(type\s+)?\{\s*type\s/.test(match[0])
    found.push({ spec: match[3], typeOnly: match[2] !== undefined || named })
  }
  return found
}

describe('module layout of the CAM core', () => {
  it('keeps every module under the size guide', () => {
    const over = sources()
      .map((file) => ({ name: relative(CAM, file), lines: lineCount(file) }))
      .filter((item) => item.lines > SIZE_GUIDE)
    expect(over).toEqual([])
  })

  it('keeps ops.ts a leaf of the type graph: only type imports and common types', () => {
    const ops = join(CAM, 'ops.ts')
    const allowed = new Set(['./feeds'])
    const values = localImports(ops).filter((item) => !item.typeOnly && !allowed.has(item.spec))
    expect(values).toEqual([])
  })

  it('keeps the kind arms out of ops.ts', () => {
    const text = readFileSync(join(CAM, 'ops.ts'), 'utf8')
    expect(text).not.toMatch(/interface\s+(FaceOperation|RectPocketOperation|ContourOperation)\b/)
  })
})
