// Tool-measure subsystem: the phases the dialog animates, the run advancing on TICK, and the
// length it writes into the tool table. The layer is the reducer, so the behaviour is pinned here
// and not by eye.
import { describe, expect, it } from 'vitest'
import { initialState, reducer } from './machine'
import { MEASURE_TOTAL_S, initialToolMeasure, measureFrame, tickToolMeasure, toolMeasureReducer } from './tool-measure'
import type { MachineState, ToolMeasureState } from './types'

const running = (elapsed: number): ToolMeasureState => ({ open: true, running: true, elapsed, length: null })

describe('замер инструмента: кадры', () => {
  it('до пуска и после завершения кадр покоится', () => {
    expect(measureFrame(initialToolMeasure)).toEqual({ phase: 'idle', depth: 0, triggered: false, progress: 0 })
    expect(measureFrame({ open: true, running: false, elapsed: MEASURE_TOTAL_S, length: 60 })).toEqual({
      phase: 'done',
      depth: 0,
      triggered: false,
      progress: 1,
    })
  })

  it('фазы идут друг за другом: быстрый, медленный, касание, отвод', () => {
    expect(measureFrame(running(0.5)).phase).toBe('rapid')
    expect(measureFrame(running(2)).phase).toBe('fine')
    expect(measureFrame(running(3.5)).phase).toBe('touch')
    expect(measureFrame(running(3.9)).phase).toBe('retract')
    expect(measureFrame(running(3.5)).triggered).toBe(true)
  })
})

describe('замер инструмента: пуск и ход', () => {
  it('открытый замер запускается, а к концу записывает длину', () => {
    const opened = reducer(initialState, { type: 'TOOL_MEASURE' })
    expect(opened.toolMeasure.open).toBe(true)
    expect(opened.toolMeasure.running).toBe(false)

    let s = reducer(opened, { type: 'TOOL_MEASURE_START' })
    expect(s.toolMeasure.running).toBe(true)

    for (let i = 0; i < 80 && s.toolMeasure.running; i++) s = reducer(s, { type: 'TICK', dt: 100 })

    expect(s.toolMeasure.running).toBe(false)
    expect(s.toolMeasure.length).not.toBeNull()
    expect(s.tools[initialState.cursor.tool].length).toBe(s.toolMeasure.length)
    expect(s.message?.text).toContain('длина')
  })

  it('аварийный стоп прерывает замер', () => {
    const started = reducer(reducer(initialState, { type: 'TOOL_MEASURE' }), { type: 'TOOL_MEASURE_START' })
    const aborted = reducer({ ...started, run: 'ESTOP' }, { type: 'TICK', dt: 100 })

    expect(aborted.toolMeasure.running).toBe(false)
    expect(aborted.message?.text).toBe('Замер прерван')
  })

  it('закрытие возвращает исходное состояние', () => {
    const opened = reducer(initialState, { type: 'TOOL_MEASURE' })
    expect(reducer(opened, { type: 'TOOL_MEASURE_CLOSE' }).toolMeasure).toEqual(initialToolMeasure)
  })
})

describe('замер инструмента: отказы', () => {
  it('во время цикла замер не открывается', () => {
    const busy: MachineState = { ...initialState, run: 'RUN' }
    expect(toolMeasureReducer(busy, { type: 'TOOL_MEASURE' }).msg).toBe('Остановите программу перед замером')
  })

  it('пустая таблица инструмента отвечает сообщением', () => {
    const empty: MachineState = { ...initialState, tools: [] }
    expect(toolMeasureReducer(empty, { type: 'TOOL_MEASURE' }).msg).toBe('Инструмент не выбран')
    expect(tickToolMeasure({ ...initialState, toolMeasure: running(1) }, 100).msg).toBeUndefined()
  })

  it('пуск при аварии отклонён', () => {
    const opened = reducer({ ...initialState, run: 'ALARM' }, { type: 'TOOL_MEASURE' })
    expect(toolMeasureReducer(opened, { type: 'TOOL_MEASURE_START' }).msg).toBe('Сначала сбросьте аварию')
  })
})
