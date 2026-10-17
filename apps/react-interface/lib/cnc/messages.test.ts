// Проверка строки сообщений: уровень задаёт время жизни; ошибка держится до
// следующего сообщения или сброса, прочие гаснут по таймеру.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'

describe('сообщения', () => {
  it('ошибка не гаснет по времени', () => {
    const err = reducer(initialState, { type: 'ESTOP' })

    expect(err.message?.ttl).toBe(0)
    expect(reducer(err, { type: 'TICK', dt: 60000 }).message?.text).toBe('АВАРИЙНЫЙ СТОП')
  })

  it('информационное сообщение гаснет по таймеру', () => {
    const info = reducer(initialState, { type: 'RESET' })

    expect(info.message?.ttl ?? 0).toBeGreaterThan(0)
    expect(reducer(info, { type: 'TICK', dt: 60000 }).message).toBeNull()
  })
})
