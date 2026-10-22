// Service subsystem: the service screens (the machine INI parameters): section and axis
// navigation, entering and leaving an edit, the write lock and the password, saving and
// resetting to defaults, and the parameter adjust. Extracted from machine.ts after the
// pattern of ladder.ts and prog.ts: the reducer composes the subsystems, and the size of
// the machine is held by the tools/check.ps1 mark.
//
// A few cases fall back to the whole machine (a screen change, the ladder edit), so the
// caller passes its reducer as dispatch - here the subsystem hands the action back up
// instead of importing machine.ts. A result is { s, msg, level }; machine.ts shows the
// message.
import { clamp } from './clamp'
import { isBusy } from './guards'
import type { Action } from './machine'
import { cancelEdit, markSaved, setParam } from './service-edit'
import { INI_FILE, SERVICE_PASSWORD, defaultParams, hasServiceList, paramAt, unsavedIds } from './service-data'
import { AXES, SERVICE_SECTIONS, type Axis, type MachineState, type Message, type ServiceSection } from './types'

export type ServiceAction =
  | { type: 'SERVICE_SECTION'; section: ServiceSection }
  | { type: 'SERVICE_AXIS'; axis: Axis }
  | { type: 'SERVICE_ENTER' }
  | { type: 'SERVICE_CANCEL' }
  | { type: 'SERVICE_BACK' }
  | { type: 'SERVICE_LOCK' }
  | { type: 'SERVICE_AUTH_INPUT'; value: string }
  | { type: 'SERVICE_SAVE' }
  | { type: 'SERVICE_DEFAULTS' }
  | { type: 'PARAM_ADJUST'; dir: 1 | -1; coarse?: boolean }

const SERVICE_TYPES = new Set<string>([
  'SERVICE_SECTION',
  'SERVICE_AXIS',
  'SERVICE_ENTER',
  'SERVICE_CANCEL',
  'SERVICE_BACK',
  'SERVICE_LOCK',
  'SERVICE_AUTH_INPUT',
  'SERVICE_SAVE',
  'SERVICE_DEFAULTS',
  'PARAM_ADJUST',
])

export const isServiceAction = (a: { type: string }): a is ServiceAction => SERVICE_TYPES.has(a.type)

export type Dispatch = (s: MachineState, a: Action) => MachineState

export type Result = { s: MachineState; msg?: string; level?: Message['level'] }

export function serviceReducer(s: MachineState, a: ServiceAction, dispatch: Dispatch): Result {
  switch (a.type) {
    case 'SERVICE_SECTION': {
      const base = cancelEdit(s)
      return {
        s: {
          ...base,
          service: { ...base.service, section: a.section, focus: 'nav' },
          cursor: { ...base.cursor, service: 0 },
        },
      }
    }

    case 'SERVICE_AXIS':
      if (s.service.editing || s.service.auth) return { s }
      return { s: { ...s, service: { ...s.service, axis: a.axis } } }

    case 'SERVICE_ENTER': {
      const sv = s.service
      if (sv.auth) {
        if (sv.auth.input === SERVICE_PASSWORD) {
          return {
            s: { ...s, service: { ...sv, auth: null, write: true } },
            msg: 'Запись параметров разрешена',
            level: 'warn',
          }
        }
        return { s: { ...s, service: { ...sv, auth: { input: '', error: true } } } }
      }
      if (sv.editing) {
        const def = paramAt(sv.section, s.cursor.service, sv.axis)
        const committed: MachineState = { ...s, service: { ...sv, editing: null } }
        return { s: def?.free ? markSaved(committed, def.id, s.params[def.id]) : committed }
      }
      if (sv.focus === 'nav') {
        if (!hasServiceList(sv.section)) return { s, msg: 'В этом разделе нет редактируемых параметров' }
        return { s: { ...s, service: { ...sv, focus: 'list' } } }
      }
      if (sv.section === 'ladder') return { s: dispatch(s, { type: 'LADDER_EDIT', on: true }) }
      const def = paramAt(sv.section, s.cursor.service, sv.axis)
      if (!def) return { s }
      if (!def.free) {
        if (isBusy(s)) return { s, msg: 'Изменение параметров во время цикла запрещено', level: 'warn' }
        if (!sv.write) return { s: { ...s, service: { ...sv, auth: { input: '', error: false } } } }
      }
      return { s: { ...s, service: { ...sv, editing: { id: def.id, original: s.params[def.id] } } } }
    }

    case 'SERVICE_CANCEL': {
      const sv = s.service
      if (sv.auth) return { s: { ...s, service: { ...sv, auth: null } } }
      if (sv.editing) return { s: cancelEdit(s), msg: 'Изменение отменено' }
      if (sv.focus === 'list') return { s: { ...s, service: { ...sv, focus: 'nav' } } }
      return { s }
    }

    case 'SERVICE_BACK': {
      const sv = s.service
      if (sv.auth || sv.editing || sv.focus === 'list') return { s: dispatch(s, { type: 'SERVICE_CANCEL' }) }
      if (sv.section !== SERVICE_SECTIONS[0])
        return { s: dispatch(s, { type: 'SERVICE_SECTION', section: SERVICE_SECTIONS[0] }) }
      return { s: dispatch(s, { type: 'SET_SCREEN', screen: 'pos' }) }
    }

    case 'SERVICE_LOCK': {
      const sv = s.service
      if (sv.auth) return { s }
      if (sv.write) {
        if (sv.editing || unsavedIds(s.params, sv.saved).length > 0) {
          return { s, msg: 'Есть несохранённые изменения — сначала нажмите «Сохранить»', level: 'warn' }
        }
        return { s: { ...s, service: { ...sv, write: false } }, msg: 'Запись параметров заблокирована' }
      }
      if (isBusy(s)) return { s, msg: 'Запись параметров недоступна во время цикла', level: 'warn' }
      return { s: { ...s, service: { ...sv, auth: { input: '', error: false } } } }
    }

    case 'SERVICE_AUTH_INPUT':
      if (!s.service.auth) return { s }
      return { s: { ...s, service: { ...s.service, auth: { input: a.value.slice(0, 12), error: false } } } }

    case 'SERVICE_SAVE': {
      const sv = s.service
      if (!sv.write) return { s, msg: 'Запись заблокирована — разрешите запись (F2)', level: 'warn' }
      if (sv.editing) return { s, msg: 'Завершите правку параметра: Ввод или Отмена', level: 'warn' }
      if (isBusy(s)) return { s, msg: 'Сохранение недоступно во время цикла', level: 'warn' }
      const count = unsavedIds(s.params, sv.saved).length
      if (count === 0) return { s, msg: 'Нет несохранённых изменений' }
      return {
        s: { ...s, service: { ...sv, saved: { ...s.params } } },
        msg: sv.restartPending
          ? `Сохранено в ${INI_FILE}: ${count}. Требуется перезапуск LinuxCNC`
          : `Сохранено в ${INI_FILE}: ${count}`,
        level: sv.restartPending ? 'warn' : 'info',
      }
    }

    case 'SERVICE_DEFAULTS': {
      const sv = s.service
      if (sv.auth || sv.editing || sv.focus !== 'list') return { s }
      const def = paramAt(sv.section, s.cursor.service, sv.axis)
      if (!def) return { s }
      const value = defaultParams()[def.id]
      if (s.params[def.id] === value) return { s, msg: 'Параметр уже имеет значение по умолчанию' }
      if (!def.free) {
        if (isBusy(s)) return { s, msg: 'Изменение параметров во время цикла запрещено', level: 'warn' }
        if (!sv.write) return { s, msg: 'Параметр защищён — разрешите запись (F2)', level: 'warn' }
      }
      let n = setParam(s, def, value)
      if (def.free) n = markSaved(n, def.id, value)
      return {
        s: n,
        msg: def.free ? 'Сброшено по умолчанию' : 'Сброшено по умолчанию. Нажмите «Сохранить», чтобы записать',
      }
    }

    case 'PARAM_ADJUST': {
      const sv = s.service
      if (sv.section === 'ladder')
        return sv.focus === 'list' ? { s: dispatch(s, { type: 'LADDER_COL', dir: a.dir }) } : { s }
      if (sv.auth) return { s }
      if (!sv.editing) {
        if (sv.focus === 'list' && (sv.section === 'joints' || sv.section === 'pid')) {
          const axis = AXES[(AXES.indexOf(sv.axis) + a.dir + AXES.length) % AXES.length]
          return { s: { ...s, service: { ...sv, axis } } }
        }
        return { s }
      }
      const def = paramAt(sv.section, s.cursor.service, sv.axis)
      if (!def || def.id !== sv.editing.id) return { s }
      const cur = s.params[def.id]
      let next: number
      if (def.options) {
        next = (cur + a.dir + def.options.length) % def.options.length
      } else {
        const k = 10 ** Math.max(def.digits, 4)
        next = Math.round(clamp(cur + a.dir * def.step * (a.coarse ? 10 : 1), def.min, def.max) * k) / k
      }
      return { s: next === cur ? s : setParam(s, def, next) }
    }
  }
}
