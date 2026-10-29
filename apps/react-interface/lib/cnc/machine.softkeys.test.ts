// Reducer layer check: the machine behaviour is fixed here, not by eye on the screen.
// The tests hold the machine state and the message words - what the operator sees.
import { describe, expect, it } from 'vitest'
import { initialState } from './machine'
import { run } from './machine.test-helpers'
import { getSoftkeys, SOFTKEY_SLOTS } from './softkeys'
import { MODES, SCREENS, type MachineState } from './types'

describe('время', () => {
  it('TICK убавляет время сообщения и смазки', () => {
    const state: MachineState = { ...initialState, message: { text: 'тест', level: 'info', ttl: 1000 }, lube: 500 }
    const s = run(state, { type: 'TICK', dt: 250 })

    expect(s.message?.ttl).toBe(750)
    expect(s.lube).toBe(250)
  })
})

describe('софт-кеи', () => {
  it('на каждом экране ровно SOFTKEY_SLOTS слотов', () => {
    for (const screen of SCREENS) {
      for (const mode of MODES) {
        expect(getSoftkeys({ ...initialState, screen, mode })).toHaveLength(SOFTKEY_SLOTS)
      }
    }
  })

  it('подпись каждого кея — непустая строка', () => {
    for (const screen of SCREENS) {
      for (const key of getSoftkeys({ ...initialState, screen })) {
        if (key) expect(key.label.trim().length).toBeGreaterThan(0)
      }
    }
  })
})
