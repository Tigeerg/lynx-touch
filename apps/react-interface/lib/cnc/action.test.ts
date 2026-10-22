// The action registry: every subsystem declares an action union and a predicate that claims it.
// When the two drift, the reducer falls through to `default: return s` and the key does nothing,
// without a word - exactly what CLAUDE.md section 6.2 forbids ("a disabled softkey explains
// itself"). The union is a type, not a value, so the check reads the sources: the declared
// literals against the claimed ones, both ways round. Two predicate forms exist - a set of types
// and a name prefix.
import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'

const SUBSYSTEMS = [
  'control',
  'cycle',
  'editor',
  'ladder',
  'mpg',
  'nav',
  'probe-wizard',
  'prog',
  'service',
  'setup',
  'test',
] as const

/** The branches the machine handles itself, by type. */
const MACHINE_TYPES = ['THEME_TOGGLE', 'UI_MODE_SET', 'TOOLPATH_TOGGLE', 'CALC', 'ALARM_ACK', 'ALARM_CLEAR_ALL', 'HELP']

const source = (name: string) => readFileSync(new URL(`./${name}.ts`, import.meta.url), 'utf8')

/** The literals of the action union: every `type: '...'` inside `export type XAction = ...`. */
const unionTypes = (src: string) => {
  const lines = src.replace(/\r/g, '').split('\n')
  const start = lines.findIndex((line) => /export type \w+Action =/.test(line))
  expect(start, 'объединение действий не найдено').toBeGreaterThanOrEqual(0)

  const body: string[] = []
  for (let i = start; i < lines.length; i++) {
    if (i > start && !lines[i].startsWith('  |')) break
    body.push(lines[i])
  }
  return [...body.join('\n').matchAll(/type:\s*'([A-Z_0-9]+)'/g)].map((m) => m[1] as string).sort()
}

/** What the predicate claims: the set it lists, or the prefix it tests. */
const claimedTypes = (src: string, declared: string[]) => {
  const set = /const [A-Z_]+_TYPES = new Set<string>\(\[([\s\S]*?)\]\)/.exec(src)
  if (set) return [...set[1].matchAll(/'([A-Z_0-9]+)'/g)].map((m) => m[1] as string).sort()
  const prefix = /a\.type\.startsWith\('([A-Z_]+)'\)/.exec(src)
  return prefix ? declared.filter((type) => type.startsWith(prefix[1])) : []
}

const claimedOf = (name: string) => claimedTypes(source(name), unionTypes(source(name)))

describe('реестр действий: объединение подсистемы и её предикат', () => {
  it.each(SUBSYSTEMS)('%s: предикат заявляет ровно объявленные действия', (name) => {
    const src = source(name)
    const declared = unionTypes(src)

    expect(declared.length).toBeGreaterThan(0)
    expect(claimedOf(name)).toEqual(declared)
    expect(src).toMatch(new RegExp(`export const is[A-Za-z]+Action`))
  })

  it('действия подсистемы с префиксным предикатом носят её приставку', () => {
    const prefixed = SUBSYSTEMS.filter((name) => !/const [A-Z_]+_TYPES = new Set/.test(source(name)))

    expect(prefixed.length).toBeGreaterThan(0)
    for (const name of prefixed) {
      const src = source(name)
      const prefix = /startsWith\('([A-Z_]+)'\)/.exec(src)![1]

      expect(
        unionTypes(src).every((type) => type.startsWith(prefix)),
        name,
      ).toBe(true)
    }
  })

  it('ни одно действие не заявлено двумя подсистемами', () => {
    const claimed = SUBSYSTEMS.flatMap((name) => claimedOf(name))
    const duplicated = claimed.filter((type, i) => claimed.indexOf(type) !== i)

    expect(duplicated).toEqual([])
  })

  it('собственные ветки машины не заявлены подсистемами', () => {
    const claimed = SUBSYSTEMS.flatMap((name) => claimedOf(name))

    expect(MACHINE_TYPES.filter((type) => claimed.includes(type))).toEqual([])
  })
})
