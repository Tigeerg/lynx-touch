// The console keyboard: which key maps to which action. The modal help must swallow the keys
// behind it (the operator pressed H, then Enter started the cycle through the overlay), and the
// emergency stop must stay reachable from Shift+Esc. The layout itself is checked once without
// the help, so moving it out of the hook did not change the usual map.
import { describe, expect, it } from 'vitest'
import { keyToAction } from './keys'
import { initialState } from './machine'
import type { MachineState } from './types'

const key = (code: string, shiftKey = false): KeyboardEvent => ({ code, shiftKey }) as KeyboardEvent

// Everything the overlay used to pass through: cycle keys, functions, tabs, arrows, axes,
// workplace and the overrides. F1..F8 and Enter never reach keyToAction while the help is open
// in the hook either; here the key map itself must not hand them out.
const HELP_BLOCKED = [
  'Enter',
  'NumpadEnter',
  'Space',
  'F1',
  'F4',
  'F8',
  'Digit1',
  'Digit5',
  'ArrowLeft',
  'ArrowRight',
  'ArrowUp',
  'ArrowDown',
  'PageUp',
  'KeyM',
  'KeyX',
  'KeyY',
  'KeyZ',
  'KeyA',
  'Minus',
  'Equal',
  'BracketLeft',
  'BracketRight',
  'Digit9',
  'Digit0',
]

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
