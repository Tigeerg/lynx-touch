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
import { AXES, type Axis, type ParamSection, type ServiceSection } from './types'

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

/** Sections whose list length is a constant: every one but the ladder, whose list is state-driven
 * (ladderRungs, through list.ts) and has no number to give here. */
export type ListSection = Exclude<ServiceSection, 'ladder'>

// One registry per question, so the length, the row lookup and the displayed list cannot drift:
// three separate switches over the section used to disagree (serviceListLength('hal') said 30 while
// sectionParams('hal') returned no rows).
const SECTION_LENGTH: Record<ListSection, number> = {
  system: SYSTEM_PARAMS.length,
  linuxcnc: LINUXCNC_PARAMS.length,
  motion: MOTION_PARAMS.length,
  joints: JOINT_PARAMS.length,
  pid: PID_PARAMS.length,
  mesa: MESA_PARAMS.length,
  hal: HAL_ROW_COUNT,
  help: 0,
}

export const serviceListLength = (section: ListSection): number => SECTION_LENGTH[section]

/** The catalogue rows of a parameter section, built from its table: no filter drops rows. */
const SECTION_PARAMS: Record<ParamSection, (axis: Axis) => readonly ParamDef[]> = {
  system: () => SYSTEM_PARAMS,
  linuxcnc: () => LINUXCNC_PARAMS,
  motion: () => MOTION_PARAMS,
  mesa: () => MESA_PARAMS,
  joints: (axis) => JOINT_PARAMS.map((def) => toParam(def, axis)),
  pid: (axis) => PID_PARAMS.map((def) => toParam(def, axis)),
}

export const sectionParams = (section: ParamSection, axis: Axis): readonly ParamDef[] => SECTION_PARAMS[section](axis)

/**
 * The catalogue row at an index; past the end is null (a stale screen cursor). A section without
 * parameters is a type error to ask, not an empty list.
 */
export const paramAt = (section: ParamSection, index: number, axis: Axis): ParamDef | null =>
  sectionParams(section, axis)[index] ?? null

export const SERVICE_PASSWORD = '1234'

export const unsavedIds = (params: Record<string, number>, saved: Record<string, number>) =>
  Object.keys(params).filter((id) => params[id] !== saved[id])

export const hasServiceList = (section: ServiceSection) => section !== 'help'

export function defaultParams(): Record<string, number> {
  const out: Record<string, number> = {}
  for (const d of [...SYSTEM_PARAMS, ...LINUXCNC_PARAMS, ...MOTION_PARAMS, ...MESA_PARAMS]) out[d.id] = d.default
  for (const d of [...JOINT_PARAMS, ...PID_PARAMS]) for (const a of AXES) out[`${a}.${d.key}`] = d.defaults[a]
  return out
}

/** The catalogue defaults, built once: the fallback for a key the state does not carry yet. */
export const PARAM_DEFAULTS: Record<string, number> = defaultParams()

/**
 * The value of a parameter. A key the state does not carry (an older machine INI, a section added
 * later) falls back to the catalogue default, so a screen shows the default and an adjust does not
 * write NaN (CLAUDE.md, section 8). Read parameters through this rather than by index:
 * `Record<string, number>` cannot promise the key is there.
 */
export const paramValue = (params: Record<string, number>, id: string): number => params[id] ?? PARAM_DEFAULTS[id] ?? 0

/** A number as the screen shows it: the option text, or the value with the row's digits. */
export function formatValue(def: ParamDef, value: number) {
  if (def.options) return def.options[value] ?? String(value)
  return value.toFixed(def.digits)
}

/** The row's value in this state, shown; a missing key shows the catalogue default. */
export const formatParam = (def: ParamDef, params: Record<string, number>) =>
  formatValue(def, paramValue(params, def.id))

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
export { MESA_BOARD_PARAM, MESA_IPADDR_PARAM, MESA_PARAMS } from './params/mesa'
export { MOTION_PARAMS } from './params/motion'
export { PID_PARAMS } from './params/pid'
export { SYSTEM_PARAMS } from './params/system'
export type { HalGroup, HalPinDef, HalPinType } from './hal'
export type { JointParamDef, ParamDef } from './params/def'
