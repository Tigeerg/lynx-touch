// The action layer's shape: machine.ts composes the subsystems through a registry, and the shared
// shapes (Result, Dispatch, Action) live in the leaf subsystem.ts, not in machine.ts. Source-level
// checks: the drift this guards against - a subsystem whose predicate is not registered, or an
// Action that again pulls machine.ts into the module graph - is invisible to a behavioural test,
// because the reducer still falls through to `default: return s` and the key does nothing, silently
// (CLAUDE.md, section 6.2). Same reading-the-source approach as action.test.ts.
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { initialState } from './machine'
import { defineSubsystem, type Dispatch } from './subsystem'

const source = (name: string) => {
  const url = new URL(`./${name}.ts`, import.meta.url)
  return existsSync(url) ? readFileSync(url, 'utf8') : ''
}

/**
 * A re-export target as written in the source: `export * from '<path>'`, `export * as ns from
 * '<path>'` or `export { … } from '<path>'`. Covers both quote styles and an optional `;`.
 */
const REEXPORT = /\bexport\s+(?:\*(?:\s+as\s+\w+)?|\{[^}]*\})\s*from\s*['"]([^'"]+)['"]/g

/** Only relative specifiers are sources of this tree; a package re-export is not ours to read. */
const isRelative = (spec: string) => spec.startsWith('./') || spec.startsWith('../')

/** The file a specifier resolves to: `.ts`/`.tsx` appended, or the directory's `index.ts`. */
const resolveModule = (spec: string, from: string | URL): URL | null => {
  const base = new URL(spec, from)
  const appended = [new URL(`${spec}.ts`, from), new URL(`${spec}.tsx`, from)]
  const index = new URL(`${base.pathname}/index.ts`, base)
  return [...appended, index].find((url) => existsSync(url) && statSync(url).isFile()) ?? null
}

/**
 * A module plus everything it re-exports, transitively: a facade (ladder.ts: `export * from
 * './ladder/...'`) may point at a submodule that re-exports again, and the predicate or the union
 * must be found wherever it settled. A specifier is relative to the module it sits in; a directory
 * resolves to its `index.ts`. Cycles and repeated reads are cut by the visited set, and the text is
 * the module first, then its re-exports in declaration order (CLAUDE.md, section 2).
 */
const sourcesWithReexports = (name: string) => {
  const visited = new Set<string>()
  const texts: string[] = []
  const walk = (url: URL) => {
    if (visited.has(url.href)) return
    visited.add(url.href)
    const own = existsSync(url) ? readFileSync(url, 'utf8') : ''
    texts.push(own)
    for (const [, spec] of own.matchAll(REEXPORT)) {
      if (!isRelative(spec)) continue
      const target = resolveModule(spec, url)
      if (target) walk(target)
    }
  }
  const root = resolveModule(`./${name}`, new URL('.', import.meta.url))
  if (root) walk(root)
  return texts
}

/** The source of a subsystem plus its transitive re-exports: the predicate may live any depth down. */
const sourceWithReexports = (name: string) => sourcesWithReexports(name).join('\n')

const machine = source('machine')
const leaf = source('subsystem')

const PREDICATE = /export const (is[A-Za-z]+Action)/

/**
 * The predicate each subsystem module exports (`export const isXxxAction`), by module. A subsystem
 * that lives in a directory (cam/, cycle/, ladder/) is named after the directory, as the registry
 * imports it; its own module and its submodules are read through the directory walk.
 */
const subsystemPredicates = () => {
  const skip = new Set(['machine', 'subsystem'])
  const isDir = (url: URL) => existsSync(url) && statSync(url).isDirectory()
  const names = readdirSync(new URL('.', import.meta.url), { withFileTypes: true })
    .filter((e) => e.isDirectory() || (e.isFile() && e.name.endsWith('.ts') && !e.name.endsWith('.test.ts')))
    .map((e) => e.name.replace(/\.ts$/, ''))
    .filter((name) => !skip.has(name))
  const found = new Set<string>()
  for (const name of names) {
    const dir = new URL(`${name}/`, new URL('.', import.meta.url))
    if (!isDir(dir)) {
      const match = PREDICATE.exec(sourceWithReexports(name))
      if (match) found.add(match[1])
      continue
    }
    for (const file of readdirSync(dir)) {
      if (!file.endsWith('.ts') || file.endsWith('.test.ts')) continue
      const match = PREDICATE.exec(sourceWithReexports(`${name}/${file.replace(/\.ts$/, '')}`))
      if (match) found.add(match[1])
    }
  }
  return [...found].sort()
}

describe('реестр действий: машина сводит подсистемы, а не впитывает их', () => {
  it('листовой модуль держит Result, Dispatch и Action', () => {
    expect(leaf).toMatch(/export type Result\b/)
    expect(leaf).toMatch(/export type Dispatch\b/)
    expect(leaf).toMatch(/export type Action =/)
  })

  it('листовой модуль и подсистемы не тянут machine.ts: круга в графе нет', () => {
    expect(leaf).not.toMatch(/from '\.\/machine'/)
    for (const name of ['nav', 'service', 'cycle', 'ladder', 'control', 'mpg']) {
      // Through the re-exports: a facade that reaches machine.ts only via a submodule is the same circle.
      expect(sourceWithReexports(name), name).not.toMatch(/from '(?:\.\.?\/)+machine'/)
    }
  })

  it('machine.ts регистрирует предикат каждой подсистемы', () => {
    const registered = [...machine.matchAll(/claims: (is[A-Za-z]+Action)/g)].map((m) => m[1]).sort()

    expect(registered.length).toBeGreaterThan(0)
    expect(registered).toEqual(subsystemPredicates())
  })

  it('machine.ts не держит цепочку if по подсистемам', () => {
    expect(machine).not.toMatch(/if \(is[A-Za-z]+Action\(a\)\)/)
  })

  it('запись реестра пропускает чужое действие и своё сводит', () => {
    const entry = defineSubsystem({
      claims: (a): a is { type: 'TICK'; dt: number } => a.type === 'TICK',
      reduce: (s) => ({ s, msg: 'своё' }),
    })
    const dispatch: Dispatch = (s) => s

    expect(entry.reduce(initialState, { type: 'HELP' }, dispatch)).toBeNull()
    expect(entry.reduce(initialState, { type: 'TICK', dt: 16 }, dispatch)).toEqual({ s: initialState, msg: 'своё' })
  })
})
