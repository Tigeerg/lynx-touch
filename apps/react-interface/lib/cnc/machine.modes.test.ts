// Reducer layer check: the machine behaviour is fixed here, not by eye on the screen.
// The tests hold the machine state and the message words - what the operator sees.
import { describe, expect, it } from 'vitest'
import { MODE_LABEL } from './data'
import { initialState } from './machine'
import { from, run } from './machine.test-helpers'
import { MODE_START_SCREEN, MODES, SCREENS } from './types'

describe('режимы и экраны', () => {
  it('SET_MODE меняет режим и уводит на стартовый экран', () => {
    const s = from({ type: 'SET_MODE', mode: 'JOG' })

    expect(s.mode).toBe('JOG')
    expect(s.screen).toBe(MODE_START_SCREEN.JOG)
    expect(s.message?.text).toBe(`Режим: ${MODE_LABEL.JOG}`)
  })

  it('SET_MODE во время выполнения запрещён', () => {
    const s = run({ ...initialState, run: 'RUN' }, { type: 'SET_MODE', mode: 'JOG' })

    expect(s.mode).toBe(initialState.mode)
    expect(s.message?.text).toBe('Смена режима во время выполнения запрещена')
  })

  it('CYCLE_MODE идёт по кругу', () => {
    const last = MODES[MODES.length - 1]
    const s = run({ ...initialState, mode: last }, { type: 'CYCLE_MODE', dir: 1 })

    expect(s.mode).toBe(MODES[0])
  })

  it('все экраны доступны в любом режиме', () => {
    for (const mode of MODES) {
      for (const screen of SCREENS) {
        const s = run({ ...initialState, mode }, { type: 'SET_SCREEN', screen })

        expect(s.screen).toBe(screen)
      }
    }
  })
})
