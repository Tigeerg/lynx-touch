// Wall-clock stamps for the panel. The value is read outside the reducer and travels in the action:
// a Date inside the reducer made the tests time-dependent and StrictMode produce two different
// stamps for one alarm log (CLAUDE.md, section 3: no silent change; the reducer stays pure).
// Formatters are pure; only stampAction reads the clock.
import type { Action } from './subsystem'

const STAMP_OPTIONS: Intl.DateTimeFormatOptions = {
  day: '2-digit',
  month: '2-digit',
  year: 'numeric',
  hour: '2-digit',
  minute: '2-digit',
}

/** "22.10.2026 14:30" - a file or ladder scheme modification mark. */
export const formatStamp = (at: number) => new Date(at).toLocaleString('ru-RU', STAMP_OPTIONS).replace(',', '')

/** "14:30:15" - the alarm log. */
export const formatTime = (at: number) => new Date(at).toLocaleTimeString('ru-RU', { hour12: false })

/**
 * Fills `at` on the actions that write a stamp, from the clock, before the pure reducer sees them.
 * The UI runs every action through this once (use-cnc-machine); a reducer call never reads the clock.
 */
export const stampAction = (a: Action, now = Date.now()): Action => {
  switch (a.type) {
    case 'EDIT_INSERT':
    case 'EDIT_DELETE':
    case 'EDIT_COMMIT':
    case 'EDIT_CANCEL':
    case 'SEARCH_REPLACE_ALL':
    case 'PROG_RENUMBER':
    case 'BROKEN_TOOL_CHECK':
    case 'LADDER_NEW':
    case 'LADDER_COPY':
    case 'LADDER_SAVE':
    case 'ESTOP':
    case 'CYCLE_START':
    case 'CAM_INSERT':
      return a.at === undefined ? { ...a, at: now } : a
    default:
      return a
  }
}
