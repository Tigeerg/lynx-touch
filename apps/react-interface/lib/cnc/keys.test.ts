// The console keyboard: which key maps to which action. The modal help must swallow the keys
// behind it (the operator pressed H, then Enter started the cycle through the overlay), and the
// emergency stop must stay reachable from Shift+Esc. The layout itself is checked once without
// the help, so moving it out of the hook did not change the usual map.
import { describe, expect, it } from 'vitest'
import { HELP_BLOCKED, keyToAction, routeKey, type KeyLike } from './keys'
import { initialState } from './machine'
import { getSoftkeys } from './softkeys'
import type { MachineState } from './types'

const key = (code: string, shiftKey = false): KeyLike => ({ code, shiftKey })

describe('справка: модальное окно не пропускает клавиши', () => {
  const help: MachineState = { ...initialState, help: true }

  it('H и Esc закрывают справку', () => {
    expect(keyToAction(help, key('KeyH'))).toEqual({ type: 'HELP', open: false })
    expect(keyToAction(help, key('Escape'))).toEqual({ type: 'HELP', open: false })
  })

  it('любая другая клавиша при открытой справке не даёт действия', () => {
    for (const code of HELP_BLOCKED) {
      expect(keyToAction(help, key(code)), code).toBeNull()
    }
  })
})

describe('аварийный стоп', () => {
  it('Shift+Esc — ESTOP, а не закрытие диалога', () => {
    expect(keyToAction(initialState, key('Escape', true))).toEqual({ type: 'ESTOP' })
  })
})

describe('раскладка без справки', () => {
  it('прежние клавиши ведут туда же', () => {
    expect(keyToAction(initialState, key('Enter'))).toEqual({ type: 'CYCLE_START' })
    expect(keyToAction(initialState, key('Space'))).toEqual({ type: 'FEED_HOLD' })
    expect(keyToAction(initialState, key('Escape'))).toEqual({ type: 'RESET' })
    expect(keyToAction(initialState, key('KeyH'))).toEqual({ type: 'HELP' })
    expect(keyToAction(initialState, key('KeyM'))).toEqual({ type: 'SET_WORKPLACE', workplace: 'TEST' })
    expect(keyToAction(initialState, key('Digit2'))).toEqual({ type: 'SET_SCREEN', screen: 'offset' })
  })
})

describe('routeKey: early branches the old hook swallowed', () => {
  it('Shift+Esc and Pause stop the machine even while typing', () => {
    expect(routeKey(initialState, key('Escape', true), true)).toMatchObject({
      action: { type: 'ESTOP' },
      prevent: true,
    })
    expect(routeKey(initialState, key('Pause'), true)).toMatchObject({ action: { type: 'ESTOP' }, prevent: true })
  })

  it('an ordinary key in a field does not reach the machine', () => {
    expect(routeKey(initialState, key('Enter'), true)).toBeNull()
    expect(routeKey(initialState, key('KeyH'), true)).toBeNull()
  })

  it('an F key under help is swallowed and does nothing', () => {
    const help = { ...initialState, help: true }
    expect(routeKey(help, key('F4'))).toEqual({ action: null, prevent: true })
  })

  it('a disabled softkey returns its reason instead of silence', () => {
    const stopped = { ...initialState, run: 'RUN' as const }
    const index = getSoftkeys(stopped).findIndex((k) => k?.disabled)
    expect(index).toBeGreaterThanOrEqual(0)
    const route = routeKey(stopped, key(`F${index + 1}`))
    expect(route?.prevent).toBe(true)
    expect(route?.action).toBeNull()
    expect(route?.hint).toBeTruthy()
  })

  it('V cycles the view only on the work screens', () => {
    expect(routeKey(initialState, key('KeyV'))).toEqual({
      action: { type: 'PATH_VIEW_CYCLE' },
      prevent: true,
    })
  })
})
