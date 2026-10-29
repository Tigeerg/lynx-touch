// The ladder constants and predicates: limits, address and operand patterns, coil kinds and their
// labels, the reference signal list, the initial schemes, the presets, and the data helpers the
// scan, the editor and the views share. Everything here is data or a pure predicate; the reducer
// (ladder-reducer.ts) and the parse (ladder-parse.ts) build on top of it.
import type { CoilKind, LadderCoil, LadderContact, LadderMem, LadderRung, LadderState } from './types'

export const LADDER_MAX_CONTACTS = 5
export const LADDER_MAX_RUNGS = 16
export const LADDER_MAX_BRANCHES = 6

export const ADDR_RE = /^[IQM]\d{1,2}\.[0-7]$/
export const TIMER_RE = /^T\d{1,2}$/
export const CTR_RE = /^C\d{1,2}$/
/** A constant in a comparison: whole or fractional (mm, seconds, pulses). */
export const NUMBER_RE = /^-?\d+(?:\.\d+)?$/
export const SIGNAL_RE = /^[IQM]\d{1,2}\.[0-7]$/
export const OPERAND_RE = /^(?:-?\d+(?:\.\d+)?|[IQM]\d{1,2}\.[0-7]|[CT]\d{1,2})$/
export const CMP_RE = /^\s*([A-Za-z0-9_.+-]+)\s*(>=|<=|<>|=|>|<)\s*([A-Za-z0-9_.+-]+)\s*$/
export const NAME_RE = /^[a-z0-9_]{1,24}$/

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

export const knownLabel = (addr: string) => LADDER_SIGNALS.find((s) => s.addr === addr)?.label

export const sig = (addr: string, nc?: boolean): LadderContact => ({
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
  signals: {},
}
