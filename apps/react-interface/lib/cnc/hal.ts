// The HAL pin reference the monitor screen shows: the board and machine pins, with the joint
// feedback, following-error and homed lines expanded from AXES, plus the derived values that
// read the panel state (halValues).
import { motionSpeed, toWork } from './coords'
import { AXES, type MachineState } from './types'

export type HalPinType = 'bit' | 'float' | 's32'
export type HalPinDef = { name: string; type: HalPinType; dir: 'IN' | 'OUT'; note?: string }
export type HalGroup = { title: string; pins: HalPinDef[] }

const axisPins = (suffix: string, type: HalPinType, dir: 'IN' | 'OUT'): HalPinDef[] =>
  AXES.map((_, i) => ({ name: `joint.${i}.${suffix}`, type, dir }))

export const HAL_GROUPS: HalGroup[] = [
  {
    title: 'Питание и безопасность',
    pins: [
      { name: 'iocontrol.0.emc-enable-in', type: 'bit', dir: 'IN' },
      { name: 'halui.estop.is-activated', type: 'bit', dir: 'OUT' },
      { name: 'halui.machine.is-on', type: 'bit', dir: 'OUT' },
    ],
  },
  {
    title: 'Режим и программа',
    pins: [
      { name: 'halui.mode.is-auto', type: 'bit', dir: 'OUT' },
      { name: 'halui.mode.is-mdi', type: 'bit', dir: 'OUT' },
      { name: 'halui.mode.is-manual', type: 'bit', dir: 'OUT' },
      { name: 'halui.program.is-running', type: 'bit', dir: 'OUT' },
      { name: 'halui.program.is-paused', type: 'bit', dir: 'OUT' },
      { name: 'halui.program.is-idle', type: 'bit', dir: 'OUT' },
      { name: 'motion.program-line', type: 's32', dir: 'OUT' },
      { name: 'halui.feed-override.value', type: 'float', dir: 'OUT' },
    ],
  },
  {
    title: 'Движение',
    pins: [
      { name: 'motion.in-position', type: 'bit', dir: 'OUT' },
      { name: 'motion.current-vel', type: 'float', dir: 'OUT' },
      { name: 'joint.2.pos-cmd', type: 'float', dir: 'OUT', note: 'рабочая координата Z' },
    ],
  },
  { title: 'Оси: обратная связь по положению', pins: axisPins('pos-fb', 'float', 'IN') },
  { title: 'Оси: ошибка слежения', pins: axisPins('f-error', 'float', 'OUT') },
  { title: 'Оси: базирование', pins: axisPins('homed', 'bit', 'OUT') },
  {
    title: 'Шпиндель и СОЖ',
    pins: [
      { name: 'spindle.0.on', type: 'bit', dir: 'OUT' },
      { name: 'spindle.0.speed-out', type: 'float', dir: 'OUT' },
      { name: 'iocontrol.0.coolant-flood', type: 'bit', dir: 'OUT' },
    ],
  },
  {
    title: 'Инструмент',
    pins: [{ name: 'iocontrol.0.tool-number', type: 's32', dir: 'OUT' }],
  },
]

export const HAL_ROW_COUNT = HAL_GROUPS.reduce((n, g) => n + g.pins.length, 0)

/**
 * The values the monitor shows, derived from the panel's own state: the panel has no HAL link yet
 * (README: there is no exchange with the control so far). A pin name is the contract between this
 * map and HAL_GROUPS, so every pin must appear here - lib/cnc/hal.test.ts holds that.
 */
export function halValues(s: MachineState): Record<string, boolean | number> {
  const moving = !!s.motion
  // homed follows the homing motion and in-position follows the motion flag: the panel has no
  // per-axis homed state to show, so these are the best it knows.
  const homed = !s.homing
  const values: Record<string, boolean | number> = {
    'iocontrol.0.emc-enable-in': s.run !== 'ESTOP',
    'halui.estop.is-activated': s.run === 'ESTOP',
    'halui.machine.is-on': s.run !== 'ESTOP' && s.run !== 'ALARM',
    'halui.mode.is-auto': s.mode === 'AUTO',
    'halui.mode.is-mdi': s.mode === 'MDI',
    'halui.mode.is-manual': s.mode === 'JOG' || s.mode === 'MPG',
    'halui.program.is-running': s.run === 'RUN',
    'halui.program.is-paused': s.run === 'HOLD',
    'halui.program.is-idle': s.run === 'IDLE',
    'motion.program-line': s.run === 'IDLE' ? 0 : s.line + 1,
    'halui.feed-override.value': s.feedOverride / 100,
    'motion.in-position': !moving,
    'motion.current-vel': motionSpeed(s) / 60,
    'joint.2.pos-cmd': toWork(s).Z,
    'spindle.0.on': s.spindleOn,
    'spindle.0.speed-out': s.spindleOn ? (s.spindleSet * s.spindleOverride) / 100 : 0,
    'iocontrol.0.coolant-flood': s.coolant,
    'iocontrol.0.tool-number': s.tool,
  }
  AXES.forEach((a, i) => {
    values[`joint.${i}.pos-fb`] = s.machine[a]
    values[`joint.${i}.f-error`] = moving ? 0.0021 + i * 0.0004 : 0.0001
    values[`joint.${i}.homed`] = homed
  })
  return values
}
