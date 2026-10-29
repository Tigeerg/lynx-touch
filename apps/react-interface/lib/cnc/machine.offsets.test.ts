// Reducer layer check: the machine behaviour is fixed here, not by eye on the screen.
// The tests hold the machine state and the message words - what the operator sees.
import { describe, expect, it } from 'vitest'
import { initialState } from './machine'
import { run } from './machine.test-helpers'
import { getSoftkeys } from './softkeys'
import type { MachineState } from './types'

describe('обнуление смещений', () => {
  const dirty: MachineState = {
    ...initialState,
    cursor: { ...initialState.cursor, offset: 2 },
    offsets: initialState.offsets.map((o, i) =>
      i === 2 || i === 3 ? { ...o, values: { ...o.values, X: 11, Y: 22, Z: 33, A: 44 } } : o,
    ),
  }

  it('OFFSET_CLEAR обнуляет только выбранную систему, остальные остаются', () => {
    const s = run(dirty, { type: 'OFFSET_CLEAR' })

    expect(s.offsets[2].values).toEqual({ X: 0, Y: 0, Z: 0, A: 0 })
    expect(s.offsets[3].values).toEqual(dirty.offsets[3].values)
    expect(s.offsets[0].values).toEqual(dirty.offsets[0].values)
    expect(s.message?.text).toContain('обнулено')
  })

  it('привязка A на F5, обнуление выбранной системы на F6', () => {
    const keys = getSoftkeys({ ...dirty, screen: 'offset' })

    expect(keys[4]?.label).toBe('Привязка A')
    expect(keys[5]?.label).toBe(`Обнулить ${dirty.offsets[2].code}`)
    expect(keys[5]?.action).toEqual({ type: 'OFFSET_CLEAR' })
  })
})
