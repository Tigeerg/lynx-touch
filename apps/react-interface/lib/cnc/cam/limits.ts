// The machine limits the check reads: the panel parameter tables, not a second opinion
// (PLAN-QUICKPROG, 8).
//
// The top spindle speed is [SPINDLE_0].MAX_FORWARD_VELOCITY and the fastest feed is
// [JOINT_0].MAX_VELOCITY of 250 mm/s (15000 mm/min): the same numbers the demo writes in
// `core/limits.h`. One place, so a check and a form do not drift apart.
import { JOINT_PARAMS } from '../params/joints'
import { MOTION_PARAMS } from '../params/motion'

const axisLimit = (key: string): number => {
  const row = JOINT_PARAMS.find((r) => r.key === key)
  if (row === undefined) throw new Error(`Параметр оси ${key} не найден в таблице станка.`)
  // X is the first axis: the slope the demo reads `defaults[0]` for.
  return row.defaults.X
}

const spindleLimit = (): number => {
  const row = MOTION_PARAMS.find((r) => r.group === 'SPINDLE_0' && r.key === 'MAX_FORWARD_VELOCITY')
  if (row === undefined) throw new Error('Параметр шпинделя MAX_FORWARD_VELOCITY не найден в таблице станка.')
  return row.max
}

/** The limits of the stand, mm/min, rev/min and mm. */
export interface MachineLimits {
  maxRpm: number
  maxFeedMmMin: number
  minToolDiameterMm: number
  maxToolDiameterMm: number
}

/** The limits of the demo simulation, read once from the parameter tables. */
export const MACHINE_LIMITS: Readonly<MachineLimits> = Object.freeze({
  maxRpm: spindleLimit(),
  maxFeedMmMin: axisLimit('MAX_VELOCITY') * 60,
  // The table cannot hold a tool wider than its opening; a few hundred mm is well above the demo.
  minToolDiameterMm: 0.1,
  maxToolDiameterMm: 200,
})
