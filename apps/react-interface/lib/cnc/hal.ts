// The HAL pin reference the monitor screen shows: the board and machine pins, with the joint
// feedback, following-error and homed lines expanded from AXES. Data only.
import { AXES } from './types'

export type HalPinType = 'bit' | 'float' | 's32'
export type HalPinDef = { name: string; type: HalPinType; dir: 'IN' | 'OUT' }
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
      { name: 'axis.z.pos-cmd (раб.)', type: 'float', dir: 'OUT' },
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
