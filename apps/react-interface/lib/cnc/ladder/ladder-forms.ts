// The ladder element form: open, edit, pick and commit the properties of a contact or a coil, and
// the scheme rename form. The element cases live in ladder-reducer.ts, which hands the form actions
// here, and the ones this module does not handle go on to the scheme manager (ladder-schemes.ts).
// The data comes from ladder-data.ts, the comparison parse from ladder-parse.ts.
import type { Result } from '../subsystem'
import type { LadderCoil, LadderContact, LadderRung, LadderState, MachineState } from '../types'
import {
  ADDR_RE,
  CTR_RE,
  DEFAULT_PRESET_COUNT,
  DEFAULT_PRESET_MS,
  LADDER_SIGNALS,
  NAME_RE,
  SIGNAL_RE,
  TIMER_RE,
  coilKind,
  isCounterAddr,
  isCounterKind,
  isTimerAddr,
  isTimerKind,
  knownLabel,
} from './ladder-data'
import { compareText, parseCompare } from './ladder-parse'
import { ladderCursor, pathContacts, withPathContacts } from './ladder-reducer'
import type { LadderAction } from './ladder-reducer'
import { ladderSchemeReducer } from './ladder-schemes'

const patch = (s: MachineState, p: Partial<LadderState>): MachineState => ({ ...s, ladder: { ...s.ladder, ...p } })

const replaceAt = <T>(list: T[], index: number, item: T) => list.map((x, i) => (i === index ? item : x))

const presetText = (c: LadderCoil) =>
  isTimerKind(coilKind(c))
    ? String((c.presetMs ?? DEFAULT_PRESET_MS) / 1000)
    : isCounterKind(coilKind(c))
      ? String(c.presetCount ?? DEFAULT_PRESET_COUNT)
      : ''

export const elementForm = (rung: LadderRung, path: number, col: number) => {
  const onCoil = path === 0 && col === rung.contacts.length
  const el = onCoil ? rung.coil : pathContacts(rung, path)[col]
  const compare = !!el.cmp
  const preset = onCoil ? presetText(rung.coil) : ''
  const first = compare ? compareText(el.cmp) : el.addr
  return {
    kind: 'element' as const,
    values: [first, el.label, rung.title, preset] as [string, string, string, string],
    compare,
  }
}

/** Addresses the scan can read: the reference list plus the coils this scheme defines. */
const knownSignals = (rungs: LadderRung[]) =>
  new Set([...LADDER_SIGNALS.map((s) => s.addr), ...rungs.map((r) => r.coil.addr)])

export function ladderFormReducer(s: MachineState, a: LadderAction): Result {
  const L = s.ladder

  switch (a.type) {
    case 'LADDER_FORM_OPEN': {
      if (!L.draft || L.manager) return { s }
      const { rung, path, col } = ladderCursor(s)
      return { s: patch(s, { form: elementForm(rung, path, col) }) }
    }

    case 'LADDER_FORM_SET': {
      if (!L.form) return { s }
      const values = L.form.values.map((v, i) => (i === a.index ? a.value : v))
      return { s: patch(s, { form: { ...L.form, values } as typeof L.form }) }
    }

    case 'LADDER_PICK': {
      if (L.form?.kind !== 'element' || L.form.compare) return { s }
      return { s: patch(s, { form: { ...L.form, values: [a.addr, a.label, L.form.values[2], L.form.values[3]] } }) }
    }

    case 'LADDER_FORM_CANCEL':
      return { s: patch(s, { form: null }) }

    case 'LADDER_FORM_COMMIT': {
      const form = L.form
      if (!form) return { s }

      if (form.kind === 'scheme') {
        const name = form.values[0].trim().toLowerCase()
        const title = form.values[1].trim()
        if (!NAME_RE.test(name)) return { s, msg: 'Имя: латиница, цифры и «_», до 24 символов', level: 'error' }
        if (L.schemes.some((x, i) => x.name === name && i !== form.target)) {
          return { s, msg: `Схема ${name} уже существует`, level: 'error' }
        }
        const target = L.schemes[form.target]
        if (!target) return { s: patch(s, { form: null }), msg: 'Схема не найдена', level: 'warn' }
        const schemes = replaceAt(L.schemes, form.target, { ...target, name, title: title || target.title })
        return { s: patch(s, { schemes, form: null }), msg: `Схема переименована: ${name}.lad` }
      }

      if (!L.draft) return { s: patch(s, { form: null }) }
      const { rungs, row, rung, path, contacts, col, onCoil } = ladderCursor(s)
      const title = form.values[2].trim() || rung.title

      if (form.compare) {
        const cmp = parseCompare(form.values[0])
        if (!cmp) return { s, msg: 'Условие вида: T0 >= 1, C0 = 5, M0.1 > 0, 0.5 > 0.1', level: 'error' }
        if (onCoil) return { s, msg: 'Блок сравнения нельзя поставить в катушку', level: 'error' }
        // An address the scheme does not define reads as zero in the scan, and the operator then
        // sees a comparison that is quietly false: refuse it here, where it can be explained.
        const known = knownSignals(rungs)
        const unknown = [cmp.a, cmp.b].find(
          (x) => (SIGNAL_RE.test(x) || isTimerAddr(x) || isCounterAddr(x)) && !known.has(x),
        )
        if (unknown) {
          return { s, msg: `Сигнал ${unknown} в схеме не задан — сравнение читалось бы нулём`, level: 'error' }
        }
        const label = form.values[1].trim() || `${cmp.a} ${cmp.op} ${cmp.b}`
        const list = replaceAt(contacts, col, { addr: 'CMP', label, cmp } as LadderContact)
        return {
          s: patch(s, {
            draft: replaceAt(rungs, row, { ...withPathContacts(rung, path, list), title }),
            dirty: true,
            form: null,
          }),
          msg: `Сравнение: ${label}`,
        }
      }

      const addr = form.values[0].trim().toUpperCase()
      const kind = coilKind(rung.coil)
      const timerCoil = onCoil && isTimerKind(kind)
      const counterCoil = onCoil && isCounterKind(kind)
      const validAddr = timerCoil
        ? TIMER_RE.test(addr)
        : counterCoil
          ? CTR_RE.test(addr)
          : ADDR_RE.test(addr) || (!onCoil && (TIMER_RE.test(addr) || CTR_RE.test(addr)))
      if (!validAddr) {
        return {
          s,
          msg: timerCoil
            ? 'Адрес таймера вида T0…T99'
            : counterCoil
              ? 'Адрес счётчика вида C0…C99'
              : 'Адрес вида I0.0, Q0.1, M1.2 (бит 0–7), T0 или C0',
          level: 'error',
        }
      }
      if (onCoil && !timerCoil && !counterCoil && addr.startsWith('I')) {
        return { s, msg: 'Катушка пишет только в выход Q или флаг M', level: 'error' }
      }
      let presetMs = rung.coil.presetMs
      let presetCount = rung.coil.presetCount
      if (timerCoil) {
        const sec = Number.parseFloat(form.values[3].replace(',', '.'))
        if (!Number.isFinite(sec) || sec < 0.1 || sec > 3600) {
          return { s, msg: 'Уставка таймера: от 0,1 до 3600 с', level: 'error' }
        }
        presetMs = Math.round(sec * 1000)
      } else if (counterCoil) {
        const n = Math.round(Number.parseFloat(form.values[3].replace(',', '.')))
        if (!Number.isFinite(n) || n < 1 || n > 9999) {
          return { s, msg: 'Уставка счётчика: целое от 1 до 9999', level: 'error' }
        }
        presetCount = n
      }
      const label = form.values[1].trim() || knownLabel(addr) || addr
      if (onCoil) {
        // Each unit keeps its own field: a timer's milliseconds never become a counter's pulses.
        const coil: LadderCoil = {
          ...rung.coil,
          addr,
          label,
          ...(timerCoil ? { presetMs } : {}),
          ...(counterCoil ? { presetCount } : {}),
        }
        return {
          s: patch(s, { draft: replaceAt(rungs, row, { ...rung, title, coil }), dirty: true, form: null }),
          msg: `Катушка ${addr} — ${label}`,
        }
      }
      const list = replaceAt(contacts, col, { addr, label, nc: contacts[col].nc } as LadderContact)
      return {
        s: patch(s, {
          draft: replaceAt(rungs, row, { ...withPathContacts(rung, path, list), title }),
          dirty: true,
          form: null,
        }),
        msg: `Контакт ${addr} — ${label}`,
      }
    }
    default:
      return ladderSchemeReducer(s, a)
  }
}
