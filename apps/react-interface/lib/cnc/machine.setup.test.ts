// Reducer layer check: the machine behaviour is fixed here, not by eye on the screen.
// The tests hold the machine state and the message words - what the operator sees.
import { describe, expect, it } from 'vitest'
import { initialState } from './machine'
import { run } from './machine.test-helpers'
import { getSoftkeys } from './softkeys'
import type { MachineState } from './types'

describe('макросы наладки — экран «Позиция»', () => {
  const manual: MachineState = { ...initialState, mode: 'JOG' }

  it('OFFSET_NEXT переключает систему координат по кругу', () => {
    const s = run(manual, { type: 'OFFSET_NEXT' })

    expect(s.activeOffset).toBe(1)
    expect(s.message?.text).toContain('G55')
  })

  it('GO_WORK_ZERO поднимает Z, затем доводит до X0 Y0 активной системы', () => {
    const start = run(manual, { type: 'GO_WORK_ZERO' })

    expect(start.run).toBe('RUN')
    expect(start.workZero).toBe(true)
    expect(start.motion?.target.Z).toBe(0)

    const done = run(
      start,
      { type: 'TICK', dt: 99999 },
      { type: 'TICK', dt: 99999 },
      { type: 'TICK', dt: 99999 },
      { type: 'TICK', dt: 99999 },
    )

    expect(done.run).toBe('IDLE')
    expect(done.workZero).toBe(false)
    expect(done.machine.X).toBe(initialState.offsets[0].values.X)
    expect(done.machine.Y).toBe(initialState.offsets[0].values.Y)
    expect(done.message?.text).toBe('В нуле детали')
  })

  it('GO_SAFE гасит незавершённый переход к нулю детали', () => {
    const s = run({ ...manual, workZero: true }, { type: 'GO_SAFE' })

    expect(s.workZero).toBe(false)
    expect(s.homing).toBe(true)
  })

  it('TOOL_PROBE записывает измеренную длину инструмента в шпинделе', () => {
    const s = run(manual, { type: 'TOOL_PROBE' })

    expect(s.message?.text).toContain('Датчик: T01')
    expect(s.tools[0].length).toBeGreaterThan(0)
  })

  it('TOOL_PROBE запрещён, когда в шпинделе щуп T99', () => {
    const s = run({ ...manual, tool: 99 }, { type: 'TOOL_PROBE' })

    expect(s.message?.text).toBe('В шпинделе щуп T99')
  })

  it('попап вызова: цифры и Enter выполняют T-код M6', () => {
    const s = run(manual, { type: 'TOOL_DIALOG', open: true }, { type: 'TOOL_INPUT', value: '5' }, { type: 'TOOL_RUN' })

    expect(s.tool).toBe(5)
    expect(s.toolDialog).toBe(false)
    expect(s.message?.text).toContain('M6')
    expect(s.message?.text).toContain('Зенковка')
  })

  it('попап не закрывается при пустом вводе и при неизвестном номере', () => {
    const empty = run(manual, { type: 'TOOL_DIALOG', open: true }, { type: 'TOOL_RUN' })

    expect(empty.toolDialog).toBe(true)
    expect(empty.message?.text).toBe('Введите номер инструмента')

    const ghost = run(
      empty,
      { type: 'TOOL_INPUT', value: '4' },
      { type: 'TOOL_INPUT', value: '2' },
      { type: 'TOOL_RUN' },
    )

    expect(ghost.toolDialog).toBe(true)
    expect(ghost.message?.text).toContain('отсутствует')
  })

  it('тормоз оси A блокирует jog по A, снятие — разрешает', () => {
    const blocked = run({ ...manual, aBrake: true }, { type: 'JOG', axis: 'A', dir: 1 })

    expect(blocked.machine.A).toBe(0)
    expect(blocked.message?.text).toContain('Тормоз оси A')

    const free = run({ ...manual, aBrake: false }, { type: 'JOG', axis: 'A', dir: 1 })

    expect(free.machine.A).toBeCloseTo(0.1, 3)
  })

  it('«3D щуп» открывает мастер щупа на экране смещений', () => {
    const s = run(manual, { type: 'PROBE_OPEN', open: true })

    expect(s.screen).toBe('offset')
    expect(s.probe.open).toBe(true)
  })

  it('раскладка F1-F8 «Позиция» соответствует плану наладки', () => {
    const keys = getSoftkeys({ ...initialState, screen: 'pos', mode: 'JOG' })

    expect(keys.map((k) => k?.label)).toEqual([
      'Обмер инструмента',
      '3D щуп',
      'СК G54',
      'Безопасный отвод',
      'В ноль детали',
      'Зажим оси A',
      'Вызов инструмента',
      'Калькулятор режимов',
    ])
  })
})
