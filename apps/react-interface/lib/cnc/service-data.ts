// The service screens' catalogue API: the section labels and hints, the list lengths and lookups
// the reducer and the screens use, and the defaults of the machine INI. The tables themselves
// live in params/* and the HAL reference in hal.ts; this module stays their single entry point.
import { HAL_ROW_COUNT } from './hal'
import { toParam, type ParamDef } from './params/def'
import { JOINT_PARAMS } from './params/joints'
import { LINUXCNC_PARAMS } from './params/linuxcnc'
import { MESA_PARAMS } from './params/mesa'
import { MOTION_PARAMS } from './params/motion'
import { PID_PARAMS } from './params/pid'
import { SYSTEM_PARAMS } from './params/system'
import { AXES, type Axis, type ServiceSection } from './types'

export const INI_FILE = 'vmc850.ini'

export const SERVICE_LABEL: Record<ServiceSection, string> = {
  system: 'Система',
  linuxcnc: 'LinuxCNC',
  motion: 'Траектория',
  joints: 'Оси',
  pid: 'PID',
  mesa: 'Mesa',
  hal: 'HAL',
  ladder: 'Ladder',
  help: 'Справка',
}

export const SERVICE_HINT: Record<ServiceSection, string> = {
  system: 'Интерфейс и [DISPLAY]',
  linuxcnc: '[KINS] [RS274NGC] [HAL] [EMCIO]',
  mesa: 'hostmot2 · плата, stepgen, PWM',
  motion: '[TRAJ] [SPINDLE] [EMCMOT]',
  joints: '[JOINT_n] пределы, поиск нуля',
  pid: 'Контуры регулирования',
  hal: 'Монитор пинов HAL',
  ladder: 'ClassicLadder, онлайн',
  help: 'Раскладка пульта и клавиши',
}

export function serviceListLength(section: ServiceSection) {
  if (section === 'hal') return HAL_ROW_COUNT
  if (section === 'system') return SYSTEM_PARAMS.length
  if (section === 'linuxcnc') return LINUXCNC_PARAMS.length
  if (section === 'mesa') return MESA_PARAMS.length
  if (section === 'motion') return MOTION_PARAMS.length
  if (section === 'joints') return JOINT_PARAMS.length
  if (section === 'pid') return PID_PARAMS.length
  return 1
}

export function paramAt(section: ServiceSection, index: number, axis: Axis): ParamDef | null {
  if (section === 'system') return SYSTEM_PARAMS[index] ?? null
  if (section === 'linuxcnc') return LINUXCNC_PARAMS[index] ?? null
  if (section === 'mesa') return MESA_PARAMS[index] ?? null
  if (section === 'motion') return MOTION_PARAMS[index] ?? null
  if (section === 'joints') return JOINT_PARAMS[index] ? toParam(JOINT_PARAMS[index], axis) : null
  if (section === 'pid') return PID_PARAMS[index] ? toParam(PID_PARAMS[index], axis) : null
  return null
}

export const SERVICE_PASSWORD = '1234'

export const sectionParams = (section: ServiceSection, axis: Axis): ParamDef[] =>
  Array.from({ length: serviceListLength(section) }, (_, i) => paramAt(section, i, axis)).filter(
    (d): d is ParamDef => d !== null,
  )

export const unsavedIds = (params: Record<string, number>, saved: Record<string, number>) =>
  Object.keys(params).filter((id) => params[id] !== saved[id])

export const hasServiceList = (section: ServiceSection) => section !== 'help'

export function defaultParams(): Record<string, number> {
  const out: Record<string, number> = {}
  for (const d of [...SYSTEM_PARAMS, ...LINUXCNC_PARAMS, ...MOTION_PARAMS, ...MESA_PARAMS]) out[d.id] = d.default
  for (const d of [...JOINT_PARAMS, ...PID_PARAMS]) for (const a of AXES) out[`${a}.${d.key}`] = d.defaults[a]
  return out
}

export function formatParam(def: ParamDef, value: number) {
  if (def.options) return def.options[value] ?? String(value)
  return value.toFixed(def.digits)
}

// The catalogue itself, re-exported so the screens and the reducer keep a single entry point.
export { HAL_GROUPS, HAL_ROW_COUNT } from './hal'
export { jointGroup, jointParam } from './params/def'
export {
  MAX_FEED_PARAM,
  MAX_SPINDLE_PARAM,
  SCREEN_SIZE_PARAM,
  THEME_OPTIONS,
  THEME_PARAM,
  TOOLPATH_PARAM,
  UI_MODE_PARAM,
} from './params/display'
export { JOINT_PARAMS } from './params/joints'
export { LINUXCNC_PARAMS } from './params/linuxcnc'
export { MESA_PARAMS } from './params/mesa'
export { MOTION_PARAMS } from './params/motion'
export { PID_PARAMS } from './params/pid'
export { SYSTEM_PARAMS } from './params/system'
export type { HalGroup, HalPinDef, HalPinType } from './hal'
export type { JointParamDef, ParamDef } from './params/def'
