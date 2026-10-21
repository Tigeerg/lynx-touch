// Interface texts: the strings the operator reads must be intact. U+FFFD (the
// replacement character) enters text when an encoding is damaged and shows a box
// instead of a letter on the 1024x600 panel and in the console (cp866), so every
// panel source that holds interface strings is scanned.
import { readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

const root = fileURLToPath(new URL('../..', import.meta.url))
const roots = ['app', 'components', 'lib'].map((dir) => join(root, dir))

function sourceFiles(dir: string): string[] {
  return readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const path = join(dir, entry.name)
    if (entry.isDirectory()) return sourceFiles(path)
    return /\.tsx?$/.test(entry.name) ? [path] : []
  })
}

describe('тексты интерфейса', () => {
  it('не содержат заменяющих символов U+FFFD', () => {
    const hits = roots.flatMap(sourceFiles).filter((file) => readFileSync(file, 'utf8').includes('\uFFFD'))
    expect(hits).toEqual([])
  })
})
