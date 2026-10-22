// Service catalogue: the section tables live in lib/cnc/params/* and the HAL reference in
// hal.ts, and service-data.ts stays their single entry point. These checks pin the contract the
// screens and the reducer rely on: every section resolves to its rows, the ids are unique and
// the defaults cover every row. The sections are read as their kinds, so a section without
// catalogue rows cannot be asked for one (ParamSection / MonitorSection / HelpSection).
import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import {
  HAL_GROUPS,
  HAL_ROW_COUNT,
  JOINT_PARAMS,
  LINUXCNC_PARAMS,
  MESA_BOARD_PARAM,
  MESA_IPADDR_PARAM,
  MESA_PARAMS,
  MOTION_PARAMS,
  PARAM_DEFAULTS,
  PID_PARAMS,
  SYSTEM_PARAMS,
  defaultParams,
  formatParam,
  paramAt,
  paramValue,
  sectionParams,
  serviceListLength,
  type JointParamDef,
  type ParamDef,
} from './service-data'
import { AXES, SERVICE_SECTIONS, isParamSection, type ParamSection } from './types'

type PlainParamSection = 'system' | 'linuxcnc' | 'motion' | 'mesa'

const TABLES: Record<PlainParamSection, ParamDef[]> = {
  system: SYSTEM_PARAMS,
  linuxcnc: LINUXCNC_PARAMS,
  motion: MOTION_PARAMS,
  mesa: MESA_PARAMS,
}

const JOINT_TABLES: Record<'joints' | 'pid', JointParamDef[]> = { joints: JOINT_PARAMS, pid: PID_PARAMS }

const PARAM_SECTION_NAMES: ParamSection[] = ['system', 'linuxcnc', 'motion', 'joints', 'pid', 'mesa']

describe('service catalogue', () => {
  it('resolves every section with a table to its rows', () => {
    for (const [section, rows] of Object.entries(TABLES) as [PlainParamSection, ParamDef[]][]) {
      expect(serviceListLength(section), section).toBe(rows.length)
      rows.forEach((row, i) => expect(paramAt(section, i, 'X'), `${section}[${i}]`).toEqual(row))
      expect(paramAt(section, rows.length, 'X')).toBeNull()
    }
    expect(serviceListLength('hal')).toBe(HAL_ROW_COUNT)
    expect(HAL_ROW_COUNT).toBe(HAL_GROUPS.reduce((n, g) => n + g.pins.length, 0))
  })

  it('expands the joint rows per axis', () => {
    for (const [section, rows] of Object.entries(JOINT_TABLES) as ['joints' | 'pid', JointParamDef[]][]) {
      expect(serviceListLength(section)).toBe(rows.length)
      const a = paramAt(section, 0, 'A')
      expect(a?.id).toBe(`A.${rows[0].key}`)
      expect(a?.default).toBe(rows[0].defaults.A)
      expect(a?.group).toBe('JOINT_3')
      expect(paramAt(section, rows.length, 'X')).toBeNull()
    }
  })

  it('gives every row a unique id and a default', () => {
    const defaults = defaultParams()
    for (const [section, rows] of Object.entries(TABLES) as [PlainParamSection, ParamDef[]][]) {
      const ids = rows.map((r) => r.id)
      expect(new Set(ids).size, section).toBe(ids.length)
      for (const row of rows) expect(defaults[row.id], `${section} ${row.id}`).toBe(row.default)
    }
    for (const [section, rows] of Object.entries(JOINT_TABLES) as ['joints' | 'pid', JointParamDef[]][]) {
      for (const row of rows) {
        for (const a of AXES) {
          expect(defaults[`${a}.${row.key}`], `${section} ${a}.${row.key}`).toBe(row.defaults[a])
        }
      }
    }
  })

  it('holds the HAL pin count as a number, not as the definition against itself', () => {
    expect(HAL_ROW_COUNT).toBe(30)
  })

  it('names the Mesa rows the screen reads instead of counting positions', () => {
    for (const id of [MESA_BOARD_PARAM, MESA_IPADDR_PARAM]) {
      const def = MESA_PARAMS.find((d) => d.id === id)

      expect(def?.options?.length, id).toBeGreaterThan(0)
    }
    expect(MESA_PARAMS[0].id).toBe(MESA_BOARD_PARAM)
  })

  it('keeps the identifiers unique across the whole catalogue, axes included', () => {
    const keys = Object.keys(defaultParams())
    expect(new Set(keys).size).toBe(keys.length)
  })

  it('addresses a row by index and refuses anything out of range', () => {
    expect(paramAt('system', -1, 'X')).toBeNull()
    expect(paramAt('system', 0.5, 'X')).toBeNull()
    expect(paramAt('system', Number.NaN, 'X')).toBeNull()
    expect(paramAt('system', SYSTEM_PARAMS.length, 'X')).toBeNull()
  })

  it('reads a value from the state and falls back to the catalogue default', () => {
    const def = SYSTEM_PARAMS[0]
    expect(paramValue({ [def.id]: 3 }, def.id)).toBe(3)
    expect(paramValue({}, def.id)).toBe(def.default)
    expect(PARAM_DEFAULTS[def.id]).toBe(def.default)
    expect(PARAM_DEFAULTS[JOINT_PARAMS[0].key]).toBeUndefined()
  })

  it('shows a value with the catalogue default instead of throwing', () => {
    const option = SYSTEM_PARAMS[0]
    expect(() => formatParam(option, {})).not.toThrow()
    expect(formatParam(option, {})).toBe(option.options?.[option.default])

    const numeric = SYSTEM_PARAMS.find((d) => !d.options)!
    expect(formatParam(numeric, {})).toBe(numeric.default.toFixed(numeric.digits))
  })

  it('classifies the sections by what they hold', () => {
    expect(SERVICE_SECTIONS.filter((s) => isParamSection(s))).toEqual(PARAM_SECTION_NAMES)
    // hal, ladder and help carry no catalogue row: asking paramAt for them does not compile.
    for (const section of ['hal', 'ladder', 'help'] as const) expect(isParamSection(section), section).toBe(false)
  })

  it('lists no rows for help, which has none', () => {
    expect(serviceListLength('help')).toBe(0)
  })

  it('fills the parameter section lists without holes', () => {
    for (const section of PARAM_SECTION_NAMES) {
      const rows = sectionParams(section, 'X')
      const table = section === 'joints' ? JOINT_PARAMS : section === 'pid' ? PID_PARAMS : TABLES[section]

      expect(rows.length, section).toBe(serviceListLength(section))
      expect(rows.length, section).toBe(table.length)
      rows.forEach((row, i) => {
        expect(row, `${section}[${i}]`).toBeDefined()
        expect(paramAt(section, i, 'X'), `${section}[${i}]`).toEqual(row)
      })
    }
  })

  it('keeps one registry per question instead of a switch per section', () => {
    const src = readFileSync(new URL('./service-data.ts', import.meta.url), 'utf8')

    expect(src).toMatch(/const SECTION_LENGTH: Record<ListSection, number>/)
    expect(src).toMatch(/const SECTION_PARAMS: Record<ParamSection/)
    expect(src).toMatch(/export const paramAt = \(section: ParamSection/)
    expect(src).toMatch(/export const sectionParams = \(section: ParamSection/)
    // A per-section `if` over the section is exactly the drift this replaced.
    expect(src).not.toMatch(/if \(section === '(hal|system|ladder|help)'\)/)
  })
})
