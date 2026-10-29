// The ladder editor reducer: the action union it claims, the rules for editing rungs and elements,
// the free-address picker and the new rung. The data it works with lives in ladder-data.ts; the
// navigation helpers (cursor, active scheme, path contacts, rung copy) are in ladder-nav.ts; the
// element form (open, edit, commit) is in ladder-forms.ts and the scheme manager (load, save, new,
// copy, rename, remove) in ladder-schemes.ts, which the default branch hands the action to.
import { clampIndex } from '../clamp'
import type { Result } from '../subsystem'
import {
  COIL_KIND_LABEL,
  COIL_KINDS,
  LADDER_MAX_BRANCHES,
  LADDER_MAX_CONTACTS,
  LADDER_MAX_RUNGS,
  coilKind,
  isCounterAddr,
  isCounterKind,
  isTimerAddr,
  isTimerKind,
  sig,
} from './ladder-data'
import { elementForm, ladderFormReducer } from './ladder-forms'
import { uniqueName } from './ladder-schemes'
import type { LadderCoil, LadderContact, LadderRung, LadderScheme, LadderState, MachineState } from '../types'

// The navigation helpers (the active scheme, the cursor, the path contacts, the rung copy) live in
// ladder-nav.ts. Imported by name for the cases below and re-exported, so the './ladder' facade and
// the neighbours keep one entry point; ladder-nav.ts does not import this module, no cycle.
import { cloneRungs, ladderCursor, savedRungs, withPathContacts } from './ladder-nav'
export {
  activeScheme,
  cloneRungs,
  ladderCursor,
  ladderRungs,
  pathContacts,
  savedRungs,
  withPathContacts,
} from './ladder-nav'

export type LadderAction =
  | { type: 'LADDER_EDIT'; on: boolean }
  | { type: 'LADDER_SAVE'; at?: number }
  | { type: 'LADDER_COL'; dir: 1 | -1 }
  | { type: 'LADDER_ADD_CONTACT' }
  | { type: 'LADDER_ADD_COMPARE' }
  | { type: 'LADDER_ADD_BRANCH' }
  | { type: 'LADDER_CYCLE_PATH'; dir: 1 | -1 }
  | { type: 'LADDER_TOGGLE_NC' }
  | { type: 'LADDER_ADD_RUNG' }
  | { type: 'LADDER_DELETE' }
  | { type: 'LADDER_FORM_OPEN' }
  | { type: 'LADDER_FORM_SET'; index: number; value: string }
  | { type: 'LADDER_FORM_COMMIT' }
  | { type: 'LADDER_FORM_CANCEL' }
  | { type: 'LADDER_PICK'; addr: string; label: string }
  | { type: 'LADDER_MANAGER'; open: boolean }
  | { type: 'LADDER_PICK_MOVE'; delta: number }
  | { type: 'LADDER_LOAD' }
  | { type: 'LADDER_NEW'; at?: number }
  | { type: 'LADDER_COPY'; at?: number }
  | { type: 'LADDER_RENAME' }
  | { type: 'LADDER_REMOVE' }

export const isLadderAction = (a: { type: string }): a is LadderAction => a.type.startsWith('LADDER_')

const patch = (s: MachineState, p: Partial<LadderState>): MachineState => ({ ...s, ladder: { ...s.ladder, ...p } })

const replaceAt = <T>(list: T[], index: number, item: T) => list.map((x, i) => (i === index ? item : x))

// The scheme name helper lives with the manager (ladder-schemes.ts): the reducer build needs it here.
export { uniqueName }

type FreeAddrKind = 'flag' | 'timer' | 'counter'

/**
 * The first free coil address of a kind for a new rung: it must not already be on the scheme (a
 * duplicate coil is refused on save). One function, not three near-copies.
 */
export function freeAddr(rungs: LadderRung[], kind: FreeAddrKind): string {
  const used = new Set(rungs.map((r) => r.coil.addr))
  if (kind === 'flag') {
    for (let byte = 1; byte < 100; byte++) {
      for (let bit = 0; bit < 8; bit++) {
        const addr = `M${byte}.${bit}`
        if (!used.has(addr)) return addr
      }
    }
    return 'M1.0'
  }
  const prefix = kind === 'timer' ? 'T' : 'C'
  for (let n = 0; n < 100; n++) if (!used.has(`${prefix}${n}`)) return `${prefix}${n}`
  return `${prefix}99`
}

export const newRung = (rungs: LadderRung[], n: number): LadderRung => ({
  title: `Ступень ${n}`,
  contacts: [sig('I0.0')],
  coil: { addr: freeAddr(rungs, 'flag'), label: 'Новый флаг' },
})

export function ladderReducer(s: MachineState, a: LadderAction): Result {
  const L = s.ladder

  switch (a.type) {
    case 'LADDER_EDIT': {
      if (a.on) {
        if (L.draft) return { s }
        const rungs = savedRungs(s)
        if (rungs.length === 0) return { s, msg: 'В схеме нет ступеней — правка невозможна', level: 'warn' }
        return {
          s: patch(s, { draft: cloneRungs(rungs), dirty: false, col: 0, path: 0, form: null }),
          msg: 'Правка: ←→ элемент, Shift+↑↓ — ветвь, Enter — свойства, Ins — контакт, Del — удалить',
        }
      }
      if (!L.draft) return { s }
      return {
        s: patch(s, { draft: null, dirty: false, form: null, manager: false, col: 0, path: 0 }),
        msg: L.dirty ? 'Изменения схемы отменены' : 'Выход из правки',
        level: L.dirty ? 'warn' : 'info',
      }
    }

    case 'LADDER_COL': {
      if (!L.draft || L.manager || L.form) return { s }
      const { col, contacts, path } = ladderCursor(s)
      const maxCol = path === 0 ? contacts.length : Math.max(0, contacts.length - 1)
      return { s: patch(s, { col: clampIndex(col, a.dir, maxCol + 1) }) }
    }

    case 'LADDER_CYCLE_PATH': {
      if (!L.draft || L.manager || L.form) return { s }
      const { path, branches } = ladderCursor(s)
      const next = (path + a.dir + branches.length) % branches.length
      return { s: patch(s, { path: next, col: 0 }) }
    }

    case 'LADDER_ADD_CONTACT': {
      if (!L.draft) return { s }
      const { rungs, row, rung, path, contacts, col, onCoil } = ladderCursor(s)
      if (contacts.length >= LADDER_MAX_CONTACTS) {
        return { s, msg: `В ветви не больше ${LADDER_MAX_CONTACTS} элементов`, level: 'warn' }
      }
      const index = onCoil ? contacts.length : col + 1
      const list = [...contacts.slice(0, index), sig('I0.0'), ...contacts.slice(index)]
      const next = withPathContacts(rung, path, list)
      const draft = replaceAt(rungs, row, next)
      return {
        s: patch(s, { draft, dirty: true, col: index, form: elementForm(next, path, index) }),
        msg: 'Контакт добавлен — задайте адрес',
      }
    }

    case 'LADDER_ADD_COMPARE': {
      if (!L.draft) return { s }
      const { rungs, row, rung, path, contacts, col, onCoil } = ladderCursor(s)
      if (contacts.length >= LADDER_MAX_CONTACTS) {
        return { s, msg: `В ветви не больше ${LADDER_MAX_CONTACTS} элементов`, level: 'warn' }
      }
      const index = onCoil ? contacts.length : col + 1
      const el: LadderContact = { addr: 'CMP', label: 'Сравнение', cmp: { op: '>=', a: 'T0', b: '1' } }
      const list = [...contacts.slice(0, index), el, ...contacts.slice(index)]
      const next = withPathContacts(rung, path, list)
      const draft = replaceAt(rungs, row, next)
      return {
        s: patch(s, { draft, dirty: true, col: index, form: elementForm(next, path, index) }),
        msg: 'Блок сравнения добавлен — задайте условие',
      }
    }

    case 'LADDER_ADD_BRANCH': {
      if (!L.draft) return { s }
      const { rungs, row, rung, branches } = ladderCursor(s)
      if (branches.length >= LADDER_MAX_BRANCHES) {
        return { s, msg: `Не больше ${LADDER_MAX_BRANCHES} ветвей в ступени`, level: 'warn' }
      }
      const next: LadderRung = { ...rung, or: [...(rung.or ?? []), [sig('I0.0')]] }
      const draft = replaceAt(rungs, row, next)
      const path = branches.length
      return {
        s: patch(s, { draft, dirty: true, path, col: 0, form: elementForm(next, path, 0) }),
        msg: 'Добавлена параллельная ветвь (ИЛИ)',
      }
    }

    case 'LADDER_TOGGLE_NC': {
      if (!L.draft) return { s }
      const { rungs, row, rung, path, contacts, col, onCoil } = ladderCursor(s)
      if (onCoil) {
        const kind = coilKind(rung.coil)
        const next = COIL_KINDS[(COIL_KINDS.indexOf(kind) + 1) % COIL_KINDS.length]
        const { kind: _kind, ...base } = rung.coil
        let coil: LadderCoil
        if (isTimerKind(next) || isCounterKind(next)) {
          const counter = isCounterKind(next)
          const keep = counter ? isCounterAddr(base.addr) : isTimerAddr(base.addr)
          // The presets ride along, each in its own field: a timer's milliseconds are never read
          // as a counter's pulses (the scan takes the unit's own default when the field is absent).
          coil = {
            ...base,
            addr: keep ? base.addr : freeAddr(rungs, counter ? 'counter' : 'timer'),
            label: keep ? base.label : counter ? 'Счётчик' : 'Таймер',
            kind: next,
          }
        } else if (isTimerKind(kind) || isCounterKind(kind)) {
          coil = { addr: freeAddr(rungs, 'flag'), label: 'Новый флаг', ...(next === 'coil' ? {} : { kind: next }) }
        } else {
          coil = { ...base, ...(next === 'coil' ? {} : { kind: next }) }
        }
        return {
          s: patch(s, { draft: replaceAt(rungs, row, { ...rung, coil }), dirty: true, form: null }),
          msg: `Тип катушки: ${COIL_KIND_LABEL[next]}`,
        }
      }
      const c = contacts[col]
      if (c.cmp) {
        const list = replaceAt(contacts, col, sig('I0.0'))
        return {
          s: patch(s, { draft: replaceAt(rungs, row, withPathContacts(rung, path, list)), dirty: true }),
          msg: 'Блок сравнения → контакт',
        }
      }
      const list = replaceAt(contacts, col, { ...c, nc: !c.nc })
      return {
        s: patch(s, { draft: replaceAt(rungs, row, withPathContacts(rung, path, list)), dirty: true }),
        msg: `${c.addr}: ${c.nc ? 'нормально открытый' : 'нормально закрытый'}`,
      }
    }

    case 'LADDER_ADD_RUNG': {
      if (!L.draft) return { s }
      const { rungs, row } = ladderCursor(s)
      if (rungs.length >= LADDER_MAX_RUNGS) return { s, msg: `Не больше ${LADDER_MAX_RUNGS} ступеней`, level: 'warn' }
      const rung = newRung(rungs, rungs.length + 1)
      const draft = [...rungs.slice(0, row + 1), rung, ...rungs.slice(row + 1)]
      return {
        s: {
          ...patch(s, { draft, dirty: true, path: 0, col: 0, form: elementForm(rung, 0, 0) }),
          cursor: { ...s.cursor, service: row + 1 },
        },
        msg: 'Ступень добавлена — задайте первый контакт',
      }
    }

    case 'LADDER_DELETE': {
      if (!L.draft) return { s }
      const { rungs, row, rung, path, contacts, col, onCoil } = ladderCursor(s)
      if (onCoil) {
        if (rungs.length === 1) return { s, msg: 'В схеме должна остаться хотя бы одна ступень', level: 'warn' }
        const draft = rungs.filter((_, i) => i !== row)
        return {
          s: {
            ...patch(s, { draft, dirty: true, col: 0, path: 0 }),
            cursor: { ...s.cursor, service: Math.min(row, draft.length - 1) },
          },
          msg: `Ступень «${rung.title}» удалена`,
          level: 'warn',
        }
      }
      if (path > 0 && contacts.length === 1) {
        const or = (rung.or ?? []).filter((_, i) => i !== path - 1)
        const next: LadderRung = { ...rung, or: or.length ? or : undefined }
        return {
          s: patch(s, { draft: replaceAt(rungs, row, next), dirty: true, path: Math.max(0, path - 1), col: 0 }),
          msg: 'Параллельная ветвь удалена',
          level: 'warn',
        }
      }
      if (path === 0 && contacts.length === 1) {
        return { s, msg: 'Последний контакт не удаляется. Выберите катушку, чтобы удалить ступень', level: 'warn' }
      }
      const removed = contacts[col]
      const list = contacts.filter((_, i) => i !== col)
      return {
        s: patch(s, {
          draft: replaceAt(rungs, row, withPathContacts(rung, path, list)),
          dirty: true,
          col: Math.max(0, col - 1),
        }),
        msg: removed.cmp ? 'Блок сравнения удалён' : `Контакт ${removed.addr} удалён`,
      }
    }

    default:
      return ladderFormReducer(s, a)
  }
}
