// Softkey labels: the label wraps inside the slot (two lines), so what has to fit on one line
// is a single word (CLAUDE.md, section 5: the panel is 1024 wide, the slot is a fixed width, so
// the length of the line is part of the check, not taste). SOFTKEY_LABEL_MAX is the character
// cap of a line measured for that slot; the states below cover every softkey layout of
// getSoftkeys, so a new overlong word fails here instead of being broken on the panel.
import { describe, expect, it } from 'vitest'
import { initialState } from './machine'
import { getSoftkeys, softkeyAction, SOFTKEY_LABEL_MAX } from './softkeys'
import type { MachineState } from './types'

const ladder = initialState.ladder
const service = initialState.service

const states: MachineState[] = [
  { ...initialState, fileDialog: true },
  { ...initialState, toolDialog: true },
  { ...initialState, calc: true },
  { ...initialState, search: { open: true, query: '', replace: '' } },
  { ...initialState, workplace: 'SETUP', screen: 'pos', mode: 'JOG' },
  { ...initialState, workplace: 'SETUP', screen: 'prog' },
  { ...initialState, workplace: 'TEST' },
  { ...initialState, workplace: 'RUN', screen: 'prog' },
  { ...initialState, workplace: 'RUN', screen: 'prog', editMode: true },
  { ...initialState, workplace: 'RUN', screen: 'prog', editor: { line: 0, buffer: '', inserted: false } },
  { ...initialState, screen: 'pos', mode: 'JOG' },
  { ...initialState, screen: 'offset', probe: { ...initialState.probe, open: true } },
  { ...initialState, screen: 'offset' },
  { ...initialState, screen: 'tool' },
  { ...initialState, screen: 'tool', toolClamped: false },
  { ...initialState, screen: 'alarm' },
  { ...initialState, screen: 'service' },
  { ...initialState, screen: 'service', service: { ...service, write: true } },
  {
    ...initialState,
    screen: 'service',
    service: { ...service, focus: 'list', editing: { id: 'DISPLAY.MAX_FEED_OVERRIDE', original: 200 } },
  },
  {
    ...initialState,
    screen: 'service',
    service: { ...service, section: 'ladder' },
    ladder: { ...ladder, form: { kind: 'element', values: ['I0.0', 'Контакт', 'Ступень', ''] } },
  },
  {
    ...initialState,
    screen: 'service',
    service: { ...service, section: 'ladder' },
    ladder: { ...ladder, manager: true },
  },
  {
    ...initialState,
    screen: 'service',
    service: { ...service, section: 'ladder' },
    ladder: { ...ladder, draft: ladder.schemes[0].rungs },
  },
]

describe('софт-кеи: подпись влезает в слот', () => {
  it('ни одно слово подписи не длиннее потолка строки слота', () => {
    const longestWord = (label: string) => Math.max(...label.split(/\s+/).map((w) => w.length))
    const over = states
      .flatMap((s) => getSoftkeys(s))
      .filter((k): k is NonNullable<typeof k> => !!k)
      .map((k) => k.label)
      .filter((label) => longestWord(label) > SOFTKEY_LABEL_MAX)

    expect(over).toEqual([])
  })

  it('потолок — 14 символов: замер строки слота панели, а не вкус', () => {
    expect(SOFTKEY_LABEL_MAX).toBe(14)
  })

  it('в НАЛАДКЕ нет клавиши «Таблица баз»: она дублирует вкладку «Смещения»', () => {
    const setup = getSoftkeys({ ...initialState, workplace: 'SETUP', screen: 'pos', mode: 'JOG' })

    expect(setup.some((k) => k?.label === 'Таблица баз')).toBe(false)
    // The slot stays in the set as a reserve, so F1-F8 keep their positions.
    expect(setup.filter(Boolean)).toHaveLength(8)
    expect(setup.filter((k) => k?.label === 'Резерв').length).toBeGreaterThan(0)
  })

  it('подпись смещения переживает пустой список', () => {
    const s: MachineState = { ...initialState, screen: 'offset', offsets: [] }

    expect(() => getSoftkeys(s)).not.toThrow()
    const labels = getSoftkeys(s)
      .filter((k): k is NonNullable<typeof k> => !!k)
      .map((k) => k.label)
    expect(labels.some((label) => label.includes('—'))).toBe(true)
  })

  it('подпись записи щупа переживает пустой список', () => {
    const s: MachineState = {
      ...initialState,
      screen: 'offset',
      probe: { ...initialState.probe, open: true },
      offsets: [],
    }

    expect(() => getSoftkeys(s)).not.toThrow()
  })
})

describe('softkey press', () => {
  it('a disabled key returns HINT with its reason', () => {
    const key = {
      label: 'Тест',
      icon: getSoftkeys(initialState)[0]!.icon,
      action: { type: 'HINT', text: 'x' } as const,
      disabled: true,
      reason: 'Занято',
    }
    expect(softkeyAction(key)).toEqual({ type: 'HINT', text: 'Занято' })
  })
})
