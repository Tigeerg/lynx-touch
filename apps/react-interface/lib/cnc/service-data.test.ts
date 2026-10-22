// Service catalogue: the section tables live in lib/cnc/params/* and the HAL reference in
// hal.ts, and service-data.ts stays their single entry point. These checks pin the contract the
// screens and the reducer rely on: every section resolves to its rows, the ids are unique and
// the defaults cover every row.
import { describe, expect, it } from 'vitest'
import {
  HAL_GROUPS,
  HAL_ROW_COUNT,
  JOINT_PARAMS,
  LINUXCNC_PARAMS,
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
  type ParamDef,
} from './service-data'
import { AXES, type ServiceSection } from './types'

const TABLES: Record<string, ParamDef[]> = {
  system: SYSTEM_PARAMS,
  linuxcnc: LINUXCNC_PARAMS,
  motion: MOTION_PARAMS,
  mesa: MESA_PARAMS,
}

const JOINT_TABLES = { joints: JOINT_PARAMS, pid: PID_PARAMS }

describe('service catalogue', () => {
  it('resolves every section with a table to its rows', () => {
    for (const [section, rows] of Object.entries(TABLES)) {
      expect(serviceListLength(section as ServiceSection), section).toBe(rows.length)
      rows.forEach((row, i) => expect(paramAt(section as ServiceSection, i, 'X'), `${section}[${i}]`).toEqual(row))
      expect(paramAt(section as ServiceSection, rows.length, 'X')).toBeNull()
    }
    expect(serviceListLength('hal')).toBe(HAL_ROW_COUNT)
    expect(HAL_ROW_COUNT).toBe(HAL_GROUPS.reduce((n, g) => n + g.pins.length, 0))
  })

  it('expands the joint rows per axis', () => {
    for (const [section, rows] of Object.entries(JOINT_TABLES)) {
      expect(serviceListLength(section as ServiceSection)).toBe(rows.length)
      const a = paramAt(section as ServiceSection, 0, 'A')
      expect(a?.id).toBe(`A.${rows[0].key}`)
      expect(a?.default).toBe(rows[0].defaults.A)
      expect(a?.group).toBe('JOINT_3')
      expect(paramAt(section as ServiceSection, rows.length, 'X')).toBeNull()
    }
  })

  it('gives every row a unique id and a default', () => {
    const defaults = defaultParams()
    for (const [section, rows] of Object.entries(TABLES)) {
      const ids = rows.map((r) => r.id)
      expect(new Set(ids).size, section).toBe(ids.length)
      for (const row of rows) expect(defaults[row.id], `${section} ${row.id}`).toBe(row.default)
    }
    for (const [section, rows] of Object.entries(JOINT_TABLES)) {
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

  it('keeps the identifiers unique across the whole catalogue, axes included', () => {
    const keys = Object.keys(defaultParams())
    expect(new Set(keys).size).toBe(keys.length)
  })

  it('addresses a row by index and refuses anything out of range', () => {
    expect(paramAt('system', -1, 'X')).toBeNull()
    expect(paramAt('system', 0.5, 'X')).toBeNull()
    expect(paramAt('system', Number.NaN, 'X')).toBeNull()
    expect(paramAt('system', SYSTEM_PARAMS.length, 'X')).toBeNull()
    expect(paramAt('help', 0, 'X')).toBeNull()
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

  it('fills the section lists without holes', () => {
    for (const section of [...Object.keys(TABLES), ...Object.keys(JOINT_TABLES)]) {
      const s = section as ServiceSection
      expect(sectionParams(s, 'X').length, section).toBe(serviceListLength(s))
    }
    // hal is a pin monitor: the screen renders HAL_GROUPS, there are no ParamDef rows.
    expect(sectionParams('hal', 'X')).toHaveLength(0)
  })
})
