import { clampIndex } from './clamp'
import { stamp } from './files'
import { isBusy } from './guards'
import type { Result } from './subsystem'
import type {
  CoilKind,
  CompareOp,
  LadderCoil,
  LadderCompare,
  LadderContact,
  LadderMem,
  LadderRung,
  LadderScheme,
  LadderState,
  MachineState,
} from './types'

export const LADDER_MAX_CONTACTS = 5
export const LADDER_MAX_RUNGS = 16
export const LADDER_MAX_BRANCHES = 6

const ADDR_RE = /^[IQM]\d{1,2}\.[0-7]$/
const TIMER_RE = /^T\d{1,2}$/
const CTR_RE = /^C\d{1,2}$/
/** A constant in a comparison: whole or fractional (mm, seconds, pulses). */
export const NUMBER_RE = /^-?\d+(?:\.\d+)?$/
const SIGNAL_RE = /^[IQM]\d{1,2}\.[0-7]$/
const OPERAND_RE = /^(?:-?\d+(?:\.\d+)?|[IQM]\d{1,2}\.[0-7]|[CT]\d{1,2})$/
const CMP_RE = /^\s*([A-Za-z0-9_.+-]+)\s*(>=|<=|<>|=|>|<)\s*([A-Za-z0-9_.+-]+)\s*$/
const NAME_RE = /^[a-z0-9_]{1,24}$/

export const COIL_KINDS: CoilKind[] = ['coil', 'set', 'reset', 'ton', 'tof', 'ctu', 'ctd']

export const COIL_KIND_LABEL: Record<CoilKind, string> = {
  coil: 'Катушка',
  set: 'Set (защёлка)',
  reset: 'Reset (сброс)',
  ton: 'TON · задержка включения',
  tof: 'TOF · задержка отключения',
  ctu: 'CTU · счётчик вверх',
  ctd: 'CTD · счётчик вниз',
}

export const coilKind = (c: LadderCoil): CoilKind => c.kind ?? 'coil'
export const isTimerKind = (k: CoilKind) => k === 'ton' || k === 'tof'
export const isCounterKind = (k: CoilKind) => k === 'ctu' || k === 'ctd'
export const isLatchKind = (k: CoilKind) => k === 'set' || k === 'reset'
export const isTimerAddr = (addr: string) => TIMER_RE.test(addr)
export const isCounterAddr = (addr: string) => CTR_RE.test(addr)
export const isOperand = (x: string) => OPERAND_RE.test(x.replace(/\s/g, ''))
export const DEFAULT_PRESET_MS = 1000
export const DEFAULT_PRESET_COUNT = 10

export function parseCompare(text: string): { op: CompareOp; a: string; b: string } | null {
  const m = CMP_RE.exec(text)
  if (!m) return null
  const [, a, op, b] = m
  if (!isOperand(a) || !isOperand(b)) return null
  return { op: op as CompareOp, a, b }
}

export const compareText = (c?: LadderCompare) => (c ? `${c.a} ${c.op} ${c.b}` : '')

export const emptyMem = (): LadderMem => ({ timers: {}, counters: {}, latched: {} })
export const rungBranches = (r: LadderRung): LadderContact[][] => [r.contacts, ...(r.or ?? [])]

export type SignalKind = 'in' | 'flag' | 'out' | 'timer'

export const LADDER_SIGNALS: { addr: string; label: string; kind: SignalKind }[] = [
  { addr: 'I0.0', label: 'Цепь АВ. СТОП', kind: 'in' },
  { addr: 'I0.1', label: 'Ограждение', kind: 'in' },
  { addr: 'I0.2', label: 'Давл. воздуха', kind: 'in' },
  { addr: 'I0.3', label: 'Инстр. зажат', kind: 'in' },
  { addr: 'I0.4', label: 'Уровень масла', kind: 'in' },
  { addr: 'I0.5', label: 'Уровень СОЖ', kind: 'in' },
  { addr: 'M0.0', label: 'Команда M3', kind: 'flag' },
  { addr: 'M0.1', label: 'Команда M8', kind: 'flag' },
  { addr: 'M0.2', label: 'Цикл активен', kind: 'flag' },
  { addr: 'M0.3', label: 'Есть авария', kind: 'flag' },
  { addr: 'Q0.0', label: 'Станок готов', kind: 'out' },
  { addr: 'Q0.1', label: 'Шпиндель', kind: 'out' },
  { addr: 'Q0.2', label: 'Насос СОЖ', kind: 'out' },
  { addr: 'Q0.3', label: 'Насос смазки', kind: 'out' },
  { addr: 'Q0.4', label: 'Лампа авария', kind: 'out' },
  { addr: 'Q0.5', label: 'Лампа работа', kind: 'out' },
  { addr: 'T0', label: 'Пауза смазки', kind: 'timer' },
]

const knownLabel = (addr: string) => LADDER_SIGNALS.find((s) => s.addr === addr)?.label

const sig = (addr: string, nc?: boolean): LadderContact => ({
  addr,
  label: knownLabel(addr) ?? addr,
  ...(nc ? { nc } : {}),
})

const MAIN_RUNGS: LadderRung[] = [
  { title: 'Готовность станка', contacts: [sig('I0.0'), sig('I0.1'), sig('I0.2')], coil: sig('Q0.0') },
  { title: 'Контактор шпинделя', contacts: [sig('Q0.0'), sig('M0.0'), sig('I0.3')], coil: sig('Q0.1') },
  { title: 'Насос СОЖ', contacts: [sig('Q0.0'), sig('M0.1'), sig('I0.5')], coil: sig('Q0.2') },
  {
    title: 'Пауза перед смазкой',
    contacts: [sig('Q0.0'), sig('M0.2')],
    coil: { addr: 'T0', label: 'Пауза смазки', kind: 'ton', presetMs: 3000 },
  },
  { title: 'Централизованная смазка', contacts: [sig('Q0.0'), sig('T0'), sig('I0.4')], coil: sig('Q0.3') },
  { title: 'Колонна: красный', contacts: [sig('M0.3')], coil: sig('Q0.4') },
  { title: 'Колонна: зелёный', contacts: [sig('Q0.0'), sig('M0.2'), sig('M0.3', true)], coil: sig('Q0.5') },
]

const LAMP_TEST_RUNGS: LadderRung[] = [
  { title: 'Тест: красный', contacts: [sig('I0.1', true)], coil: sig('Q0.4') },
  { title: 'Тест: зелёный', contacts: [sig('I0.0'), sig('I0.2')], coil: sig('Q0.5') },
]

export const initialLadder: LadderState = {
  schemes: [
    { name: 'plc_main', title: 'Основная логика VMC850', rungs: MAIN_RUNGS, modified: '12.03.2026 09:14' },
    { name: 'plc_lamp_test', title: 'Тест сигнальной колонны', rungs: LAMP_TEST_RUNGS, modified: '02.02.2026 16:40' },
  ],
  active: 0,
  draft: null,
  dirty: false,
  col: 0,
  path: 0,
  manager: false,
  pick: 0,
  form: null,
  mem: emptyMem(),
}

/** The active scheme; a stale index falls back to the first one rather than reading undefined. */
export const activeScheme = (s: MachineState) => s.ladder.schemes[s.ladder.active] ?? s.ladder.schemes[0]
export const savedRungs = (s: MachineState) => activeScheme(s)?.rungs ?? []
export const ladderRungs = (s: MachineState) => s.ladder.draft ?? savedRungs(s)

/** What the editor and the views read when a scheme has no rungs at all (invalid state). */
const EMPTY_RUNG: LadderRung = { title: '', contacts: [], coil: { addr: 'M1.0', label: '' } }

export function ladderCursor(s: MachineState) {
  const rungs = ladderRungs(s)
  const row = clampIndex(s.cursor.service, 0, rungs.length)
  const rung = rungs[row] ?? EMPTY_RUNG
  const branches = rungBranches(rung)
  const path = clampIndex(s.ladder.path, 0, branches.length)
  const contacts = branches[path] ?? []
  const maxCol = path === 0 ? contacts.length : Math.max(0, contacts.length - 1)
  const col = clampIndex(s.ladder.col, 0, maxCol + 1)
  return { rungs, row, rung, branches, path, contacts, col, onCoil: path === 0 && col === contacts.length }
}

export const pathContacts = (rung: LadderRung, path: number) => rungBranches(rung)[path] ?? rung.contacts

export const withPathContacts = (rung: LadderRung, path: number, contacts: LadderContact[]): LadderRung =>
  path === 0 ? { ...rung, contacts } : { ...rung, or: (rung.or ?? []).map((b, i) => (i === path - 1 ? contacts : b)) }

export type LadderAction =
  | { type: 'LADDER_EDIT'; on: boolean }
  | { type: 'LADDER_SAVE' }
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
  | { type: 'LADDER_NEW' }
  | { type: 'LADDER_COPY' }
  | { type: 'LADDER_RENAME' }
  | { type: 'LADDER_REMOVE' }

export const isLadderAction = (a: { type: string }): a is LadderAction => a.type.startsWith('LADDER_')

const cloneContact = (c: LadderContact): LadderContact => ({ ...c, ...(c.cmp ? { cmp: { ...c.cmp } } : {}) })
const cloneRungs = (rungs: LadderRung[]): LadderRung[] =>
  rungs.map((r) => ({
    ...r,
    contacts: r.contacts.map(cloneContact),
    coil: { ...r.coil },
    ...(r.or ? { or: r.or.map((b) => b.map(cloneContact)) } : {}),
  }))

const patch = (s: MachineState, p: Partial<LadderState>): MachineState => ({ ...s, ladder: { ...s.ladder, ...p } })

const replaceAt = <T>(list: T[], index: number, item: T) => list.map((x, i) => (i === index ? item : x))

function freeFlag(rungs: LadderRung[]) {
  const used = new Set(rungs.map((r) => r.coil.addr))
  for (let byte = 1; byte < 100; byte++) {
    for (let bit = 0; bit < 8; bit++) {
      const addr = `M${byte}.${bit}`
      if (!used.has(addr)) return addr
    }
  }
  return 'M1.0'
}

function freeTimer(rungs: LadderRung[]) {
  const used = new Set(rungs.map((r) => r.coil.addr))
  for (let n = 0; n < 100; n++) if (!used.has(`T${n}`)) return `T${n}`
  return 'T99'
}

function freeCounter(rungs: LadderRung[]) {
  const used = new Set(rungs.map((r) => r.coil.addr))
  for (let n = 0; n < 100; n++) if (!used.has(`C${n}`)) return `C${n}`
  return 'C99'
}

const presetText = (c: LadderCoil) =>
  isTimerKind(coilKind(c))
    ? String((c.presetMs ?? DEFAULT_PRESET_MS) / 1000)
    : isCounterKind(coilKind(c))
      ? String(c.presetCount ?? DEFAULT_PRESET_COUNT)
      : ''

function uniqueName(schemes: LadderScheme[], base: string) {
  const names = new Set(schemes.map((x) => x.name))
  if (!names.has(base)) return base
  for (let i = 2; ; i++) if (!names.has(`${base}_${i}`)) return `${base}_${i}`
}

const newRung = (rungs: LadderRung[], n: number): LadderRung => ({
  title: `Ступень ${n}`,
  contacts: [sig('I0.0')],
  coil: { addr: freeFlag(rungs), label: 'Новый флаг' },
})

const elementForm = (rung: LadderRung, path: number, col: number) => {
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

const DIRTY_BLOCK = 'Сначала сохраните (F7) или отмените правку (Esc)'

/** Addresses the scan can read: the reference list plus the coils this scheme defines. */
const knownSignals = (rungs: LadderRung[]) =>
  new Set([...LADDER_SIGNALS.map((s) => s.addr), ...rungs.map((r) => r.coil.addr)])

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
            addr: keep ? base.addr : counter ? freeCounter(rungs) : freeTimer(rungs),
            label: keep ? base.label : counter ? 'Счётчик' : 'Таймер',
            kind: next,
          }
        } else if (isTimerKind(kind) || isCounterKind(kind)) {
          coil = { addr: freeFlag(rungs), label: 'Новый флаг', ...(next === 'coil' ? {} : { kind: next }) }
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
        const unknown = [cmp.a, cmp.b].find((x) => SIGNAL_RE.test(x) && !knownSignals(rungs).has(x))
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

    case 'LADDER_MANAGER':
      return { s: patch(s, { manager: a.open, pick: L.active, form: null }) }

    case 'LADDER_PICK_MOVE':
      return { s: patch(s, { pick: clampIndex(L.pick, a.delta, L.schemes.length) }) }

    case 'LADDER_LOAD': {
      if (L.dirty) return { s, msg: DIRTY_BLOCK, level: 'warn' }
      if (isBusy(s)) return { s, msg: 'Загрузка схемы в ПЛК во время цикла запрещена', level: 'warn' }
      const scheme = L.schemes[L.pick]
      if (!scheme) return { s, msg: 'Схема не выбрана', level: 'warn' }
      return {
        s: {
          ...patch(s, {
            active: L.pick,
            draft: L.draft ? cloneRungs(scheme.rungs) : null,
            manager: false,
            col: 0,
            path: 0,
            mem: emptyMem(),
          }),
          cursor: { ...s.cursor, service: 0 },
        },
        msg: `Схема ${scheme.name}.lad загружена в ПЛК`,
      }
    }

    case 'LADDER_NEW': {
      if (L.dirty) return { s, msg: DIRTY_BLOCK, level: 'warn' }
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      const rungs = [newRung([], 1)]
      const scheme: LadderScheme = {
        name: uniqueName(L.schemes, 'plc_new'),
        title: 'Новая схема',
        rungs,
        modified: stamp(),
      }
      const schemes = [...L.schemes, scheme]
      const index = schemes.length - 1
      return {
        s: {
          ...patch(s, {
            schemes,
            active: index,
            pick: index,
            draft: cloneRungs(rungs),
            dirty: false,
            manager: false,
            col: 0,
            mem: emptyMem(),
          }),
          cursor: { ...s.cursor, service: 0 },
        },
        msg: `Создана схема ${scheme.name}.lad — она активна в ПЛК`,
        level: 'warn',
      }
    }

    case 'LADDER_COPY': {
      const src = L.schemes[L.pick]
      if (!src) return { s, msg: 'Схема не выбрана', level: 'warn' }
      const copy: LadderScheme = {
        ...src,
        name: uniqueName(L.schemes, `${src.name}_copy`),
        title: `${src.title} (копия)`,
        rungs: cloneRungs(src.rungs),
        modified: stamp(),
      }
      const at = L.pick + 1
      const schemes = [...L.schemes.slice(0, at), copy, ...L.schemes.slice(at)]
      return {
        s: patch(s, { schemes, pick: at, active: L.active >= at ? L.active + 1 : L.active }),
        msg: `Создана копия ${copy.name}.lad`,
      }
    }

    case 'LADDER_RENAME': {
      const target = L.schemes[L.pick]
      if (!target) return { s, msg: 'Схема не выбрана', level: 'warn' }
      return { s: patch(s, { form: { kind: 'scheme', target: L.pick, values: [target.name, target.title] } }) }
    }

    case 'LADDER_REMOVE': {
      if (L.schemes.length === 1) return { s, msg: 'Нельзя удалить единственную схему', level: 'warn' }
      if (L.pick === L.active) return { s, msg: 'Схема активна в ПЛК — сначала загрузите другую', level: 'warn' }
      const removed = L.schemes[L.pick]
      const schemes = L.schemes.filter((_, i) => i !== L.pick)
      return {
        s: patch(s, {
          schemes,
          active: L.active > L.pick ? L.active - 1 : L.active,
          pick: Math.min(L.pick, schemes.length - 1),
        }),
        msg: `Схема ${removed.name}.lad удалена`,
        level: 'warn',
      }
    }

    case 'LADDER_SAVE': {
      if (!L.draft) return { s }
      if (isBusy(s)) return { s, msg: 'Запись в ПЛК во время цикла запрещена', level: 'error' }
      const seen = new Map<string, { row: number; kind: CoilKind }>()
      for (const [i, r] of L.draft.entries()) {
        const kind = coilKind(r.coil)
        const prev = seen.get(r.coil.addr)
        if (prev && !(isLatchKind(prev.kind) && isLatchKind(kind))) {
          return {
            s: { ...s, cursor: { ...s.cursor, service: i } },
            msg: `Катушка ${r.coil.addr} задана дважды (ступени ${prev.row + 1} и ${i + 1})`,
            level: 'error',
          }
        }
        seen.set(r.coil.addr, { row: i, kind })
      }
      const scheme = activeScheme(s)
      const schemes = replaceAt(L.schemes, L.active, { ...scheme, rungs: L.draft, modified: stamp() })
      return {
        s: patch(s, { schemes, draft: null, dirty: false, form: null, manager: false, col: 0, mem: emptyMem() }),
        msg: `Схема ${scheme.name}.lad сохранена и загружена в ПЛК`,
      }
    }
  }
}
