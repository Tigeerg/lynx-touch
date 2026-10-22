// The service catalogue's shared row helpers: the shape of a parameter row and the builders the
// section tables use. Kept in their own module so the tables (params/*) and the API
// (service-data.ts) can both use them without a cycle.
import { type Axes, type Axis } from '../types'

export interface ParamDef {
  id: string
  group: string
  key: string
  label: string
  unit?: string
  step: number
  min: number
  max: number
  digits: number
  default: number
  options?: string[]
  free?: boolean
  restart?: boolean
}

export interface JointParamDef extends Omit<ParamDef, 'id' | 'group' | 'default'> {
  defaults: Axes
}

/** A table row: `digits` defaults to 0, the id is built from the group and the key. */
export const p = (def: Omit<ParamDef, 'id' | 'digits'> & { digits?: number }): ParamDef => ({
  digits: 0,
  ...def,
  id: `${def.group}.${def.key}`,
})

/** The per-axis defaults of a joint row. */
export const ax = (X: number, Y: number, Z: number, A: number): Axes => ({ X, Y, Z, A })

/** Off/on labels of a boolean parameter. */
export const OFF_ON = ['Нет', 'Да']

const AXIS_JOINT: Record<Axis, number> = { X: 0, Y: 1, Z: 2, A: 3 }

export const jointGroup = (axis: Axis) => `JOINT_${AXIS_JOINT[axis]}`

export const toParam = (def: JointParamDef, axis: Axis): ParamDef => {
  const { defaults, ...rest } = def
  return { ...rest, group: jointGroup(axis), id: `${axis}.${def.key}`, default: defaults[axis] }
}

export const jointParam = (def: JointParamDef, axis: Axis) => toParam(def, axis)
