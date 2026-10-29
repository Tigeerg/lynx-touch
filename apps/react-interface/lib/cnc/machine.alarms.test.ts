// Reducer layer check: the machine behaviour is fixed here, not by eye on the screen.
// The tests hold the machine state and the message words - what the operator sees.
import { describe, expect, it } from 'vitest'
import { initialState } from './machine'
import { from, run } from './machine.test-helpers'
import type { MachineState } from './types'

describe('аварийный стоп и сброс', () => {
  it('ESTOP останавливает станок и поднимает аварию 1001', () => {
    const s = run({ ...initialState, spindleOn: true, coolant: true }, { type: 'ESTOP' })

    expect(s.run).toBe('ESTOP')
    expect(s.spindleOn).toBe(false)
    expect(s.coolant).toBe(false)
    expect(s.message?.text).toBe('АВАРИЙНЫЙ СТОП')
    expect(s.alarms.some((al) => al.code === 1001 && al.active)).toBe(true)
  })

  it('повторный ESTOP переводит в ALARM и просит СБРОС', () => {
    const s = from({ type: 'ESTOP' }, { type: 'ESTOP' })

    expect(s.run).toBe('ALARM')
    expect(s.message?.text).toContain('нажмите СБРОС')
  })

  it('RESET при активном аварийном стопе отклонён', () => {
    const s = from({ type: 'ESTOP' }, { type: 'RESET' })

    expect(s.run).toBe('ESTOP')
    expect(s.message?.text).toBe('Сначала отпустите аварийный стоп')
  })

  it('RESET гасит ошибки и обнуляет счётчики', () => {
    const dirty: MachineState = {
      ...initialState,
      run: 'ALARM',
      line: 12,
      cycleTime: 900,
      alarms: [
        { id: 'a-test-error', code: 1002, level: 'error', text: 'Ошибка привода X', time: '12:00:00', active: true },
        { id: 'a-test-warning', code: 2001, level: 'warning', text: 'Износ фрезы', time: '12:00:01', active: true },
      ],
    }
    const s = run(dirty, { type: 'RESET' })

    expect(s.run).toBe('IDLE')
    expect(s.line).toBe(0)
    expect(s.cycleTime).toBe(0)
    expect(s.message?.text).toBe('Сброс выполнен')
    // RESET clears only errors: a warning stays active - that is the RESET contract.
    expect(s.alarms.find((al) => al.id === 'a-test-error')?.active).toBe(false)
    expect(s.alarms.find((al) => al.id === 'a-test-warning')?.active).toBe(true)
  })
})
