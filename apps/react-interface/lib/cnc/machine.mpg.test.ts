// Reducer layer check: the machine behaviour is fixed here, not by eye on the screen.
// The tests hold the machine state and the message words - what the operator sees.
import { describe, expect, it } from 'vitest'
import { MODE_LABEL } from './data'
import { initialState, reducer } from './machine'
import { MODE_START_SCREEN, MPG_STEPS } from './types'

describe('режим МАХОВИК', () => {
  it('SET_MODE MPG переводит на позицию и оставляет допустимый шаг маховика', () => {
    const s = reducer({ ...initialState, jogStep: 1 }, { type: 'SET_MODE', mode: 'MPG' })

    expect(s.mode).toBe('MPG')
    expect(s.screen).toBe(MODE_START_SCREEN.MPG)
    expect(MPG_STEPS).toContain(s.jogStep)
    expect(s.message?.text).toBe(`Режим: ${MODE_LABEL.MPG}`)
  })
})
