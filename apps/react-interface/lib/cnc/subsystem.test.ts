// The action layer's shape: machine.ts composes the subsystems through a registry, and the shared
// shapes (Result, Dispatch, Action) live in the leaf subsystem.ts, not in machine.ts. Source-level
// checks: the drift this guards against - a subsystem whose predicate is not registered, or an
// Action that again pulls machine.ts into the module graph - is invisible to a behavioural test,
// because the reducer still falls through to `default: return s` and the key does nothing, silently
// (CLAUDE.md, section 6.2). Same reading-the-source approach as action.test.ts.
import { existsSync, readFileSync, readdirSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { initialState } from './machine'
import { defineSubsystem, type Dispatch } from './subsystem'

const source = (name: string) => {
  const url = new URL(`./${name}.ts`, import.meta.url)
  return existsSync(url) ? readFileSync(url, 'utf8') : ''
}

const machine = source('machine')
const leaf = source('subsystem')

/** The predicate each subsystem module exports (`export const isXxxAction`), by module. */
const subsystemPredicates = () => {
  const skip = new Set(['machine', 'subsystem'])
  const names = readdirSync(new URL('.', import.meta.url))
    .filter((f) => f.endsWith('.ts') && !f.endsWith('.test.ts'))
    .map((f) => f.replace(/\.ts$/, ''))
    .filter((name) => !skip.has(name))
  const found: string[] = []
  for (const name of names) {
    const m = /export const (is[A-Za-z]+Action)/.exec(source(name))
    if (m) found.push(m[1])
  }
  return found.sort()
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
      expect(source(name), name).not.toMatch(/from '\.\/machine'/)
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
