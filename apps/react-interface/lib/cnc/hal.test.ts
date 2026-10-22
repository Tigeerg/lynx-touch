// The HAL monitor: the pin reference in hal.ts and the values the panel derives for it. The
// contract is a pin name, a string, so a rename on one end used to show 0 on the other without a
// word. These checks keep the two ends together and keep the axis annotation out of the name.
import { describe, expect, it } from 'vitest'
import { HAL_GROUPS, halValues } from './hal'
import { initialState } from './machine'

const pins = HAL_GROUPS.flatMap((g) => g.pins)

describe('монитор HAL: справочник и значения', () => {
  it('у каждого пина справочника есть источник значения', () => {
    const values = halValues(initialState)
    const missing = pins.filter((pin) => !(pin.name in values)).map((pin) => pin.name)

    expect(missing).toEqual([])
  })

  it('у каждого значения справочника есть пин: лишних ключей нет', () => {
    const names = new Set(pins.map((pin) => pin.name))
    const extra = Object.keys(halValues(initialState)).filter((key) => !names.has(key))

    expect(extra).toEqual([])
  })

  it('пометка оси живёт отдельным полем, а не внутри имени пина', () => {
    expect(pins.filter((pin) => /[(А-Яа-я]/.test(pin.name))).toEqual([])
    expect(pins.some((pin) => pin.note)).toBe(true)
  })

  it('имена пинов уникальны', () => {
    const names = pins.map((pin) => pin.name)

    expect(new Set(names).size).toBe(names.length)
  })
})
