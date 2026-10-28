// The console keyboard: which key maps to which action. The modal help must swallow the keys
// behind it (the operator pressed H, then Enter started the cycle through the overlay), and the
// emergency stop must stay reachable from Shift+Esc. The layout itself is checked once without
// the help, so moving it out of the hook did not change the usual map.
import { describe, expect, it } from 'vitest'
import { HELP_BLOCKED, keyToAction, routeKey, type KeyLike } from './keys'
import { initialState, reducer } from './machine'
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

// The MDI line lives on the SETUP work screen. While it is open the physical keyboard types the
// block there instead of pressing the panel keys, and a running PROGON simulation must not take
// the keys away: the simulation keeps `run` at IDLE, so it is not a busy machine.
describe('MDI с физической клавиатуры', () => {
  const setup: MachineState = {
    ...initialState,
    workplace: 'SETUP',
    mode: 'JOG',
    screen: 'pos',
  }
  const sim: MachineState = { ...setup, sim: { active: true, line: 3, timer: 0 } }
  const withBuffer = (s: MachineState, buffer: string): MachineState => ({ ...s, mdi: { ...s.mdi, buffer } })

  it('letters and digits go to the MDI buffer, not to the panel keys', () => {
    expect(routeKey(withBuffer(sim, 'G0 '), key('KeyX'))).toEqual({
      action: { type: 'MDI_BUFFER', value: 'G0 X' },
      prevent: true,
    })
    expect(routeKey(withBuffer(sim, 'G0 X'), key('Digit0'))).toEqual({
      action: { type: 'MDI_BUFFER', value: 'G0 X0' },
      prevent: true,
    })
  })

  it('arrows do not move the block cursor while MDI is open', () => {
    expect(routeKey(sim, key('ArrowUp'))).toEqual({ action: null, prevent: true })
    expect(routeKey(sim, key('PageDown'))).toEqual({ action: null, prevent: true })
  })

  it('Backspace erases the last character', () => {
    expect(routeKey(withBuffer(sim, 'G0 X0'), key('Backspace'))).toEqual({
      action: { type: 'MDI_BUFFER', value: 'G0 X' },
      prevent: true,
    })
  })

  it('Escape clears the MDI line instead of resetting the machine', () => {
    expect(routeKey(withBuffer(sim, 'G0 X0'), key('Escape'))).toEqual({
      action: { type: 'MDI_BUFFER', value: '' },
      prevent: true,
    })
  })

  it('Enter still runs the MDI block', () => {
    expect(routeKey(withBuffer(sim, 'G0 X0'), key('Enter'))).toEqual({
      action: { type: 'CYCLE_START' },
      prevent: true,
    })
  })

  it('the started block cancels the PROGON simulation instead of running twice', () => {
    const typed = withBuffer(sim, 'G0 X0')
    const route = routeKey(typed, key('Enter'))
    if (!route?.action) throw new Error('Enter must route the MDI block to the machine')
    const next = reducer(typed, route.action)

    expect(next.sim.active).toBe(false)
    expect(next.mdi.running).toBe(true)
    expect(next.run).toBe('RUN')
  })

  it('outside SETUP the letter keys keep their panel meaning', () => {
    expect(routeKey(initialState, key('KeyX'))).toEqual({
      action: { type: 'SELECT_AXIS', axis: 'X' },
      prevent: true,
    })
  })

  it('a busy machine does not take the MDI keys', () => {
    expect(routeKey({ ...setup, run: 'RUN' }, key('KeyG'))).toBeNull()
    expect(routeKey({ ...setup, run: 'RUN' }, key('Backspace'))).toBeNull()
  })
})

// The digit strip on the panel sends the same characters as the physical keys. Digits, minus and
// the decimal point must reach MDI without Shift; the G-code block symbols stay on Shift.
describe('MDI: цифры и знаки без Shift', () => {
  const setup: MachineState = { ...initialState, workplace: 'SETUP', mode: 'JOG', screen: 'pos' }
  const at = (buffer: string): MachineState => ({ ...setup, mdi: { ...setup.mdi, buffer } })

  it('цифры, минус и точка вводятся без Shift', () => {
    const plain: [string, string][] = [
      ['Digit1', '1'],
      ['Digit2', '2'],
      ['Digit3', '3'],
      ['Digit4', '4'],
      ['Digit5', '5'],
      ['Digit6', '6'],
      ['Digit7', '7'],
      ['Digit8', '8'],
      ['Digit9', '9'],
      ['Digit0', '0'],
      ['Minus', '-'],
      ['Period', '.'],
    ]
    for (const [code, char] of plain) {
      expect(routeKey(at('G0 X'), key(code)), code).toEqual({
        action: { type: 'MDI_BUFFER', value: `G0 X${char}` },
        prevent: true,
      })
    }
  })

  it('#, % и ( требуют Shift', () => {
    expect(routeKey(at(''), key('Digit3', true))).toEqual({
      action: { type: 'MDI_BUFFER', value: '#' },
      prevent: true,
    })
    expect(routeKey(at(''), key('Digit5', true))).toEqual({
      action: { type: 'MDI_BUFFER', value: '%' },
      prevent: true,
    })
    expect(routeKey(at(''), key('Digit9', true))).toEqual({
      action: { type: 'MDI_BUFFER', value: '(' },
      prevent: true,
    })
  })
})
