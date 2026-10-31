// The reducer stays pure: the wall-clock stamp travels in the action (stampAction fills it in the
// UI). A Date inside the reducer made the tests time-dependent and StrictMode produce two different
// stamps for one alarm log (CLAUDE.md, section 3). The check pins both ends: the reducer reads
// `at`, and stampAction is the only place that reads the clock.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import { formatStamp, formatTime, stampAction } from './time'

const AT = new Date(2026, 9, 22, 14, 30, 15).getTime()

describe('время приходит в экшене, а не берётся в редьюсере', () => {
  it('отметка аварии берётся из экшена', () => {
    expect(reducer(initialState, { type: 'ESTOP', at: AT }).alarms[0].time).toBe(formatTime(AT))
  })

  it('без времени в экшене редьюсер его не выдумывает', () => {
    expect(reducer(initialState, { type: 'ESTOP' }).alarms[0].time).toBe('')
  })

  it('одно и то же действие даёт один и тот же журнал (чистый редьюсер)', () => {
    const a = { type: 'ESTOP', at: AT } as const

    expect(reducer(initialState, a).alarms).toEqual(reducer(initialState, a).alarms)
  })

  it('метка файла берётся из экшена', () => {
    const base = reducer(initialState, { type: 'EDIT_MODE', on: true })
    const begin = reducer(base, { type: 'EDIT_BEGIN' })
    const typed = reducer(begin, { type: 'EDIT_BUFFER', value: 'N10 G0 X0' })
    const committed = reducer(typed, { type: 'EDIT_COMMIT', at: AT })

    expect(committed.files[0].modified).toBe(formatStamp(AT))
  })

  it('сторож читает часы вне редьюсера и заполняет только пишущие действия', () => {
    expect(stampAction({ type: 'ESTOP' }, AT)).toEqual({ type: 'ESTOP', at: AT })
    expect(stampAction({ type: 'TICK', dt: 16 }, AT)).toEqual({ type: 'TICK', dt: 16 })
    expect(stampAction({ type: 'ESTOP', at: AT + 1 }, AT)).toEqual({ type: 'ESTOP', at: AT + 1 })
  })

  it('нажатие Пуск получает время: по нему считается окно подтверждения новой программы', () => {
    expect(stampAction({ type: 'CYCLE_START' }, AT)).toEqual({ type: 'CYCLE_START', at: AT })
    expect(stampAction({ type: 'CYCLE_START', at: AT + 1 }, AT)).toEqual({ type: 'CYCLE_START', at: AT + 1 })
  })
})
