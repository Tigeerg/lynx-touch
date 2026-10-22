// The catalogue is data shared by every screen and by the reducer: one .sort() or .push() in any
// screen used to reorder it for the whole panel. The arrays are frozen and typed `readonly`, so a
// stray mutation throws instead of quietly changing what everyone sees (CLAUDE.md, section 3:
// no silent change; section 2: the module hands out a public interface, not its internals).
import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { OFF_ON } from './params/def'
import { JOINT_PARAMS, LINUXCNC_PARAMS, MESA_PARAMS, MOTION_PARAMS, PID_PARAMS, SYSTEM_PARAMS } from './service-data'
import { AXES, JOG_FEEDS, MODES, MPG_STEPS, SCREENS, TABS } from './types'

const CATALOGUE = {
  AXES,
  MODES,
  TABS,
  SCREENS,
  MPG_STEPS,
  JOG_FEEDS,
  OFF_ON,
  SYSTEM_PARAMS,
  LINUXCNC_PARAMS,
  MOTION_PARAMS,
  MESA_PARAMS,
  JOINT_PARAMS,
  PID_PARAMS,
}

describe('каталог: данные закрыты от правки снаружи', () => {
  it.each(Object.keys(CATALOGUE))('%s заморожен', (name) => {
    expect(Object.isFrozen(CATALOGUE[name as keyof typeof CATALOGUE]), name).toBe(true)
  })

  it('объявления помечены readonly', () => {
    const declared: [string, string[]][] = [
      ['types', ['AXES', 'MODES', 'TABS', 'SCREENS', 'MPG_STEPS', 'JOG_FEEDS']],
      ['params/def', ['OFF_ON']],
      ['params/system', ['SYSTEM_PARAMS']],
      ['params/linuxcnc', ['LINUXCNC_PARAMS']],
      ['params/motion', ['MOTION_PARAMS']],
      ['params/mesa', ['MESA_PARAMS']],
      ['params/joints', ['JOINT_PARAMS']],
      ['params/pid', ['PID_PARAMS']],
    ]
    for (const [file, names] of declared) {
      const src = readFileSync(new URL(`./${file}.ts`, import.meta.url), 'utf8')
      for (const name of names) expect(src, `${file}:${name}`).toMatch(new RegExp(`export const ${name}: readonly`))
    }
  })
})
