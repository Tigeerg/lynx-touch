// The console's own keys that no subsystem claims: alarms, help, theme, the UI mode and the
// toolpath preview. A leaf module - it imports no subsystem, so the module graph stays flat
// (CLAUDE.md, section 2). Extracted from machine.ts so a new subsystem joins the machine without
// growing it (the tools/check.ps1 mark).
import { clampIndex } from './clamp'
import { notify } from './messages'
import { paramValue, THEME_OPTIONS, THEME_PARAM, TOOLPATH_PARAM, UI_MODE_PARAM } from './service-data'
import { markSaved } from './service-edit'
import type { MachineAction } from './subsystem'
import type { MachineState } from './types'

export function machineReducer(s: MachineState, a: MachineAction): MachineState {
  switch (a.type) {
    case 'THEME_TOGGLE': {
      // A missing key (an older state) steps from the catalogue default, not from the first theme or NaN.
      const next = (paramValue(s.params, THEME_PARAM) + 1) % THEME_OPTIONS.length
      return markSaved({ ...s, params: { ...s.params, [THEME_PARAM]: next } }, THEME_PARAM, next)
    }
    case 'UI_MODE_SET':
      if (s.params[UI_MODE_PARAM] === a.value) return s
      return markSaved({ ...s, params: { ...s.params, [UI_MODE_PARAM]: a.value } }, UI_MODE_PARAM, a.value)
    case 'TOOLPATH_TOGGLE': {
      const show = s.params[TOOLPATH_PARAM] ? 0 : 1
      return notify(
        markSaved({ ...s, params: { ...s.params, [TOOLPATH_PARAM]: show } }, TOOLPATH_PARAM, show),
        show ? 'Предпросмотр траектории включён' : 'Предпросмотр траектории скрыт',
      )
    }

    case 'CALC':
      return { ...s, calc: a.open ?? !s.calc }

    case 'ALARM_ACK': {
      const i = s.cursor.alarm
      if (!s.alarms[i]) return s
      return { ...s, alarms: s.alarms.map((al, idx) => (idx === i ? { ...al, active: false } : al)) }
    }

    case 'ALARM_CLEAR_ALL': {
      const alarms = s.alarms.filter((al) => al.active)
      return { ...s, alarms, cursor: { ...s.cursor, alarm: clampIndex(s.cursor.alarm, 0, alarms.length) } }
    }

    case 'HELP':
      return { ...s, help: a.open ?? !s.help }

    default:
      return s
  }
}
