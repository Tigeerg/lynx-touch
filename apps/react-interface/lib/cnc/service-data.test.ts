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
  PID_PARAMS,
  SYSTEM_PARAMS,
  defaultParams,
  paramAt,
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

  it('fills the section lists without holes', () => {
    for (const section of [...Object.keys(TABLES), ...Object.keys(JOINT_TABLES)]) {
      const s = section as ServiceSection
      expect(sectionParams(s, 'X').length, section).toBe(serviceListLength(s))
    }
    // hal is a pin monitor: the screen renders HAL_GROUPS, there are no ParamDef rows.
    expect(sectionParams('hal', 'X')).toHaveLength(0)
  })
})
