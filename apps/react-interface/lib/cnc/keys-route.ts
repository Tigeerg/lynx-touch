// The console key routing: one routing point for keydown. The emergency stop comes first, then
// the modal help, F1..F8 (softkeys), text fields, the MDI line and finally the ordinary key map,
// which stays in keys.ts (keyToAction) together with the structural key data. The hook only
// supplies DOM facts (typing) and dispatches the result.
import { isBusy } from './guards'
import { getSoftkeys } from './softkeys'
import { keyToAction, type KeyLike, type KeyRoute } from './keys'
import type { MachineState } from './types'

// The shifted digit row, so % ( ) and the other block characters can be typed on a real console.
const SHIFTED_DIGITS: Record<string, string> = {
  Digit1: '!',
  Digit2: '@',
  Digit3: '#',
  Digit4: '$',
  Digit5: '%',
  Digit6: '^',
  Digit7: '&',
  Digit8: '*',
  Digit9: '(',
  Digit0: ')',
}

// Punctuation a G-code block needs. The codes are layout independent: a Russian keyboard still
// types the Latin addresses, because only the physical key is read here.
const PUNCTUATION: Record<string, string> = {
  Minus: '-',
  NumpadSubtract: '-',
  Equal: '=',
  NumpadAdd: '+',
  Period: '.',
  NumpadDecimal: '.',
  Comma: ',',
  Slash: '/',
  NumpadDivide: '/',
  NumpadMultiply: '*',
  Semicolon: ';',
  Quote: "'",
  BracketLeft: '[',
  BracketRight: ']',
  Backslash: '\\',
  Backquote: '`',
  Space: ' ',
}

const MDI_NAVIGATION = new Set([
  'ArrowUp',
  'ArrowDown',
  'ArrowLeft',
  'ArrowRight',
  'PageUp',
  'PageDown',
  'Home',
  'End',
  'Insert',
  'Tab',
])

// Returns the character the key types in the MDI line, or null when the key is not text.
function mdiChar(e: KeyLike): string | null {
  const letter = /^Key([A-Z])$/.exec(e.code)
  if (letter) return letter[1]
  const numpad = /^Numpad([0-9])$/.exec(e.code)
  if (numpad) return numpad[1]
  const digit = /^Digit([0-9])$/.exec(e.code)
  if (digit) return e.shiftKey ? (SHIFTED_DIGITS[digit[0]] ?? null) : digit[1]
  return PUNCTUATION[e.code] ?? null
}

// Help is modal: H and Esc close it, as in keyToAction (keys.ts).
const closesHelp = (e: KeyLike) => e.code === 'KeyH' || e.code === 'Escape'

// The MDI line is the keyboard target on the SETUP work screen. The run simulation keeps `run`
// at IDLE, so it never closes the line; only a real cycle (RUN/HOLD) takes the keys away.
const mdiOpen = (state: MachineState) => state.workplace === 'SETUP' && state.screen === 'pos' && !isBusy(state)

// A line edit action: the buffer is replaced with the whole new value, the reducer stays pure.
const mdiEdit = (value: string): KeyRoute => ({ action: { type: 'MDI_BUFFER', value }, prevent: true })

function mdiRoute(state: MachineState, e: KeyLike): KeyRoute | null {
  if (e.code === 'Backspace') return mdiEdit(state.mdi.buffer.slice(0, -1))
  if (e.code === 'Delete') return mdiEdit('')
  // Escape clears the line instead of the RESET key: a stray Esc must not cancel the PROGON
  // simulation the operator is watching.
  if (e.code === 'Escape') return mdiEdit('')
  // The block list is not on this screen and the arrows belong to the text line: no CURSOR.
  if (MDI_NAVIGATION.has(e.code)) return { action: null, prevent: true }
  const char = mdiChar(e)
  return char === null ? null : mdiEdit(state.mdi.buffer + char)
}

/**
 * One routing point for the console: emergency stop, modal help, F1..F8, text fields, the MDI
 * line and the ordinary key map. The hook only supplies DOM facts (typing) and dispatches the
 * result.
 */
export function routeKey(state: MachineState, e: KeyLike, typing = false): KeyRoute | null {
  if (e.ctrlKey || e.metaKey || e.altKey) return null

  // The emergency stop is reachable from anywhere, including a focused field or the ladder form.
  if ((e.code === 'Escape' && e.shiftKey) || e.code === 'Pause') {
    return { action: { type: 'ESTOP' }, prevent: true }
  }

  if (state.help) {
    if (closesHelp(e)) return { action: { type: 'HELP', open: false }, prevent: true }
    return { action: null, prevent: /^F[1-8]$/.test(e.code) }
  }

  const fn = /^F([1-8])$/.exec(e.code)
  if (fn) {
    const key = getSoftkeys(state)[Number(fn[1]) - 1]
    if (!key) return { action: null, prevent: true }
    if (key.disabled) {
      return { action: null, hint: key.reason ?? 'Клавиша недоступна', prevent: true }
    }
    return { action: key.action, prevent: true }
  }

  // A field or the ladder form owns the keyboard; only the emergency stop above gets through.
  if (typing || state.editor || state.ladder.form) return null

  // A dialog has its own keys; the MDI line must not swallow them while it is on top.
  const modal = state.fileDialog || state.toolDialog || state.toolMeasure.open || state.calc || state.search.open
  if (!modal && mdiOpen(state)) {
    const route = mdiRoute(state, e)
    if (route) return route
  }

  if (e.code === 'KeyV' && !state.fileDialog && (state.screen === 'pos' || state.screen === 'prog')) {
    return { action: { type: 'PATH_VIEW_CYCLE' }, prevent: true }
  }

  const action = keyToAction(state, e)
  return action ? { action, prevent: true } : null
}
