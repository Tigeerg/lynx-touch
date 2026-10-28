// The console keyboard map: a keydown becomes an action or nothing. It lives in lib so the
// layout is checked without a browser (keys.test.ts) and the hook only owns the modal/focus
// routing around it.
import { isBusy } from './guards'
import { getSoftkeys } from './softkeys'
import type { Action } from './subsystem'
import { TABS, WORKPLACES, screenOfTab, type Axis, type MachineState } from './types'

// Structural key data: lib/cnc has no DOM and stays testable without a browser.
export type KeyLike = {
  code: string
  shiftKey: boolean
  ctrlKey?: boolean
  metaKey?: boolean
  altKey?: boolean
}

export type KeyRoute = { action: Action | null; hint?: string; prevent: boolean }

const AXIS_KEYS: Record<string, Axis> = { KeyX: 'X', KeyY: 'Y', KeyZ: 'Z', KeyA: 'A' }

// Help is modal: only H and Esc close it; F1..F8 are swallowed so the browser does not act,
// and every other key returns no action while the overlay is open.
export const HELP_BLOCKED = [
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
  'PageDown',
  'KeyM',
  'KeyO',
  'KeyE',
  'KeyP',
  'KeyX',
  'KeyY',
  'KeyZ',
  'KeyA',
  'Insert',
  'Delete',
  'Minus',
  'Equal',
  'BracketLeft',
  'BracketRight',
  'Digit9',
  'Digit0',
] as const

const closesHelp = (e: KeyLike) => e.code === 'KeyH' || e.code === 'Escape'

export function keyToAction(state: MachineState, e: KeyLike): Action | null {
  // Help is modal: H and Esc close it, every other key gives no action. Without this guard the
  // overlay passed Enter, Space, the tabs, the axes and the overrides through to the machine.
  if (state.help) {
    if (closesHelp(e)) return { type: 'HELP', open: false }
    return null
  }
  if (state.toolMeasure.open) {
    if ((e.code === 'Enter' || e.code === 'NumpadEnter') && !state.toolMeasure.running) {
      return { type: 'TOOL_MEASURE_START' }
    }
    if (e.code === 'Escape') return { type: 'TOOL_MEASURE_CLOSE' }
    return null
  }
  if (state.toolDialog) {
    const digit = /^(?:Digit|Numpad)([0-9])$/.exec(e.code)
    if (digit) return { type: 'TOOL_INPUT', value: digit[1] }
    if (e.code === 'Backspace') return { type: 'TOOL_BACK' }
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'TOOL_RUN' }
    if (e.code === 'Escape') return { type: 'TOOL_DIALOG', open: false }
    return null
  }
  if (state.calc) {
    if (e.code === 'Escape') return { type: 'CALC', open: false }
    return null
  }
  if (state.search.open) {
    if (e.code === 'Escape') return { type: 'SEARCH', open: false }
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'SEARCH_FIND' }
    return null
  }
  if (state.fileDialog) {
    if (e.code === 'ArrowUp') return { type: 'FILE_CURSOR', delta: -1 }
    if (e.code === 'ArrowDown') return { type: 'FILE_CURSOR', delta: 1 }
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'FILE_OPEN' }
    if (e.code === 'Escape' || e.code === 'KeyO') return { type: 'FILE_DIALOG', open: false }
    return null
  }
  if (state.screen === 'service' && state.service.section === 'ladder' && !state.help) {
    const enter = e.code === 'Enter' || e.code === 'NumpadEnter'
    if (state.ladder.manager) {
      if (enter) return { type: 'LADDER_LOAD' }
      if (e.code === 'Insert') return { type: 'LADDER_NEW' }
      if (e.code === 'Delete') return { type: 'LADDER_REMOVE' }
      if (e.code === 'Escape' && !e.shiftKey) return { type: 'LADDER_MANAGER', open: false }
    } else if (state.ladder.draft) {
      if (e.code === 'ArrowUp' && e.shiftKey) return { type: 'LADDER_CYCLE_PATH', dir: -1 }
      if (e.code === 'ArrowDown' && e.shiftKey) return { type: 'LADDER_CYCLE_PATH', dir: 1 }
      if (e.code === 'Insert' && e.shiftKey) return { type: 'LADDER_ADD_COMPARE' }
      if (e.code === 'Insert' && e.ctrlKey) return { type: 'LADDER_ADD_BRANCH' }
      if (enter) return { type: 'LADDER_FORM_OPEN' }
      if (e.code === 'Insert') return { type: 'LADDER_ADD_CONTACT' }
      if (e.code === 'Delete') return { type: 'LADDER_DELETE' }
      if (e.code === 'Escape' && !e.shiftKey) return { type: 'LADDER_EDIT', on: false }
      if (e.code === 'ArrowLeft') return { type: 'LADDER_COL', dir: -1 }
      if (e.code === 'ArrowRight') return { type: 'LADDER_COL', dir: 1 }
    }
  }
  if (state.editMode) {
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'EDIT_BEGIN' }
    if (e.code === 'Insert') return { type: 'EDIT_INSERT' }
    if (e.code === 'Delete') return { type: 'EDIT_DELETE' }
    if (e.code === 'Escape' && !e.shiftKey && !state.help) return { type: 'EDIT_MODE', on: false }
  }
  if (e.code === 'KeyO') return { type: 'FILE_DIALOG', open: true }
  if (e.code === 'KeyE') return { type: 'EDIT_MODE' }
  if (e.code === 'KeyP' && (state.screen === 'pos' || state.screen === 'prog')) return { type: 'TOOLPATH_TOGGLE' }

  const digit = /^Digit([1-5])$/.exec(e.code)
  if (digit) return { type: 'SET_SCREEN', screen: screenOfTab(TABS[Number(digit[1]) - 1], state.workplace) }

  if (state.screen === 'service' && !state.help) {
    const sv = state.service
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'SERVICE_ENTER' }
    if (e.code === 'Escape' && !e.shiftKey && (sv.auth || sv.editing || sv.focus === 'list'))
      return { type: 'SERVICE_CANCEL' }
  }
  if (state.screen === 'service') {
    if (e.code === 'ArrowLeft') return { type: 'PARAM_ADJUST', dir: -1, coarse: e.shiftKey }
    if (e.code === 'ArrowRight') return { type: 'PARAM_ADJUST', dir: 1, coarse: e.shiftKey }
    if (AXIS_KEYS[e.code]) return { type: 'SERVICE_AXIS', axis: AXIS_KEYS[e.code] }
  }
  if (AXIS_KEYS[e.code]) return { type: 'SELECT_AXIS', axis: AXIS_KEYS[e.code] }

  switch (e.code) {
    case 'Enter':
    case 'NumpadEnter':
      return { type: 'CYCLE_START' }
    case 'Space':
      return { type: 'FEED_HOLD' }
    case 'Escape':
      if (state.help) return { type: 'HELP', open: false }
      return e.shiftKey ? { type: 'ESTOP' } : { type: 'RESET' }
    case 'Pause':
      return { type: 'ESTOP' }
    case 'KeyM': {
      const dir = e.shiftKey ? -1 : 1
      const idx = (WORKPLACES.indexOf(state.workplace) + dir + WORKPLACES.length) % WORKPLACES.length
      return { type: 'SET_WORKPLACE', workplace: WORKPLACES[idx] }
    }
    case 'KeyH':
      return { type: 'HELP' }
    case 'ArrowLeft':
      return { type: 'JOG', dir: -1 }
    case 'ArrowRight':
      return { type: 'JOG', dir: 1 }
    case 'ArrowUp':
      return { type: 'CURSOR', delta: -1 }
    case 'ArrowDown':
      return { type: 'CURSOR', delta: 1 }
    case 'PageUp':
      return { type: 'CURSOR', delta: -10 }
    case 'PageDown':
      return { type: 'CURSOR', delta: 10 }
    case 'Minus':
    case 'NumpadSubtract':
      return { type: 'OVERRIDE', kind: 'feed', dir: -1 }
    case 'Equal':
    case 'NumpadAdd':
      return { type: 'OVERRIDE', kind: 'feed', dir: 1 }
    case 'BracketLeft':
      return { type: 'OVERRIDE', kind: 'spindle', dir: -1 }
    case 'BracketRight':
      return { type: 'OVERRIDE', kind: 'spindle', dir: 1 }
    case 'Digit9':
      return { type: 'OVERRIDE', kind: 'rapid', dir: -1 }
    case 'Digit0':
      return { type: 'OVERRIDE', kind: 'rapid', dir: 1 }
    default:
      return null
  }
}

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
