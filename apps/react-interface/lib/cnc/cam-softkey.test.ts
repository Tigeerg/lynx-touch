// The connection of the quick program to the panel (PLAN-QUICKPROG, 7.4, C5): the "Программа"
// screen carries the overlay's key, the key opens and closes the same overlay the reducer owns,
// and Esc closes it before the ordinary RESET, while the emergency stop still comes first.
import { describe, expect, it } from 'vitest'

import { initialState } from './machine'
import { run } from './machine.test-helpers'
import { type KeyLike } from './keys'
import { routeKey } from './keys-route'
import { getSoftkeys, SOFTKEY_SLOTS } from './softkeys'
import { SOFTKEY_LABEL_MAX } from './softkeys'
import type { MachineState } from './types'

const key = (code: string, shiftKey = false): KeyLike => ({ code, shiftKey })

/** The "Программа" screen in НАЛАДКА: the operator's own program, not a run. */
const prog: MachineState = { ...initialState, workplace: 'SETUP', screen: 'prog' }

/** The key of the bar whose label starts with `prefix`. */
const slotOf = (s: MachineState, prefix: string) =>
  getSoftkeys(s).findIndex((k) => k !== null && k.label.startsWith(prefix))

describe('«Программа»: софт-кей «Быстрая УП»', () => {
  it('на экране есть ключ «Быстрая УП», он открывает оверлей', () => {
    const index = slotOf(prog, 'Быстрая УП')
    expect(index).toBeGreaterThanOrEqual(0)
    const k = getSoftkeys(prog)[index]
    expect(k?.action).toEqual({ type: 'CAM_OPEN' })
    expect(k?.label).toBe('Быстрая УП')
  })

  it('подпись влезает в слот: самое длинное слово не длиннее бюджета', () => {
    const index = slotOf(prog, 'Быстрая УП')
    const label = getSoftkeys(prog)[index]?.label ?? ''
    for (const word of label.split(/\s+/)) expect(word.length).toBeLessThanOrEqual(SOFTKEY_LABEL_MAX)
  })

  it('при открытом оверлее тот же слот закрывает его', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    const index = slotOf(open, 'Закрыть')
    expect(index).toBeGreaterThanOrEqual(0)
    expect(getSoftkeys(open)[index]?.action).toEqual({ type: 'CAM_CLOSE' })
    expect(getSoftkeys(open)).toHaveLength(SOFTKEY_SLOTS)
  })

  it('пока оверлей открыт, слоты не двигаются: порядок прежний', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    const index = slotOf(prog, 'Быстрая УП')
    const before = getSoftkeys(prog)
      .map((k) => k?.label)
      .filter((_, i) => i !== index)
    const after = getSoftkeys(open)
      .map((k) => k?.label)
      .filter((_, i) => i !== index)
    // The slots are compared by position, out of the place of the CAM key: that one slot shows
    // "Закрыть быструю УП" in place of "Быстрая УП", every other slot keeps its place and label.
    expect(after).toEqual(before)
  })

  it('клавиша F4 слота открывает и закрывает оверлей', () => {
    const index = slotOf(prog, 'Быстрая УП')
    const open = routeKey(prog, key(`F${index + 1}`))
    expect(open?.action).toEqual({ type: 'CAM_OPEN' })
    const opened = run(prog, { type: 'CAM_OPEN' })
    const close = routeKey(opened, key(`F${index + 1}`))
    expect(close?.action).toEqual({ type: 'CAM_CLOSE' })
  })
})

describe('Esc при открытом оверлее', () => {
  it('Esc закрывает быструю УП, а не сбрасывает станок', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    expect(routeKey(open, key('Escape'))).toEqual({ action: { type: 'CAM_CLOSE' }, prevent: true })
  })

  it('Esc в поле формы тоже закрывает оверлей: фокус ввода его не прячет', () => {
    const open = run(prog, { type: 'CAM_OPEN' }, { type: 'CAM_OP', kind: 'pocket' })
    expect(routeKey(open, key('Escape'), true)).toEqual({ action: { type: 'CAM_CLOSE' }, prevent: true })
  })

  it('Shift+Esc при открытом оверлее — аварийный стоп, а не закрытие', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    expect(routeKey(open, key('Escape', true))?.action).toEqual({ type: 'ESTOP' })
  })

  it('без оверлея Esc остаётся сбросом', () => {
    expect(routeKey(prog, key('Escape'))?.action).toEqual({ type: 'RESET' })
  })
})

describe('оверлей забирает клавиатуру у других', () => {
  it('V не переключает вид, пока открыта быстрая УП', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    expect(routeKey(open, key('KeyV'))).toBeNull()
  })

  it('цифровой ввод не переключает экраны', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    expect(routeKey(open, key('Digit2'))).toBeNull()
  })

  it('остальные F-клавиши в оверлее остаются за панелью', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    const camSlot = slotOf(open, 'Быстрая УП')
    const bar = getSoftkeys(open)
    const other = bar.findIndex((k, i) => i !== camSlot && k !== null)
    expect(other).toBeGreaterThanOrEqual(0)
    expect(routeKey(open, key(`F${other + 1}`))?.action).toEqual(bar[other]?.action)
  })
})
