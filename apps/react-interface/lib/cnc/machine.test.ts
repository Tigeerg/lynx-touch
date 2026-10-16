// Проверка слоя редьюсера: поведение станка фиксируется здесь, а не глазами на экране.
// Тесты держатся за состояние станка и слова сообщений — то, что видит оператор.
import { describe, expect, it } from 'vitest'
import { MODE_LABEL } from './data'
import { currentFile, initialState, reducer, type Action } from './machine'
import { getSoftkeys, SOFTKEY_SLOTS } from './softkeys'
import { buildToolpath, simPose } from './toolpath'
import { MODE_LOCKED_SCREENS, MODE_START_SCREEN, MODES, SCREENS, type MachineState } from './types'

const run = (state: MachineState, ...actions: Action[]) => actions.reduce(reducer, state)
const from = (...actions: Action[]) => run(initialState, ...actions)

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
    const dirty: MachineState = { ...initialState, run: 'ALARM', line: 12, cycleTime: 900 }
    const s = run(dirty, { type: 'RESET' })

    expect(s.run).toBe('IDLE')
    expect(s.line).toBe(0)
    expect(s.cycleTime).toBe(0)
    expect(s.message?.text).toBe('Сброс выполнен')
    expect(s.alarms.every((al) => al.level !== 'error' || !al.active)).toBe(true)
  })
})

describe('режимы и экраны', () => {
  it('SET_MODE меняет режим и уводит на стартовый экран', () => {
    const s = from({ type: 'SET_MODE', mode: 'JOG' })

    expect(s.mode).toBe('JOG')
    expect(s.screen).toBe(MODE_START_SCREEN.JOG)
    expect(s.message?.text).toBe(`Режим: ${MODE_LABEL.JOG}`)
  })

  it('SET_MODE во время выполнения запрещён', () => {
    const s = run({ ...initialState, run: 'RUN' }, { type: 'SET_MODE', mode: 'JOG' })

    expect(s.mode).toBe(initialState.mode)
    expect(s.message?.text).toBe('Смена режима во время выполнения запрещена')
  })

  it('CYCLE_MODE идёт по кругу', () => {
    const last = MODES[MODES.length - 1]
    const s = run({ ...initialState, mode: last }, { type: 'CYCLE_MODE', dir: 1 })

    expect(s.mode).toBe(MODES[0])
  })

  it('закрытый в режиме экран не открывается и говорит почему', () => {
    const locked = MODE_LOCKED_SCREENS.AUTO[0]
    const s = from({ type: 'SET_SCREEN', screen: locked })

    expect(s.screen).toBe(initialState.screen)
    expect(s.message?.text).toContain('заблокирован')
  })

  it('разрешённый экран открывается', () => {
    const allowed = SCREENS.find((sc) => sc !== 'service' && !MODE_LOCKED_SCREENS.AUTO.includes(sc))

    expect(allowed).toBeTruthy()
    const s = from({ type: 'SET_SCREEN', screen: allowed! })

    expect(s.screen).toBe(allowed)
  })
})

describe('время', () => {
  it('TICK убавляет время сообщения и смазки', () => {
    const state: MachineState = { ...initialState, message: { text: 'тест', level: 'info', ttl: 1000 }, lube: 500 }
    const s = run(state, { type: 'TICK', dt: 250 })

    expect(s.message?.ttl).toBe(750)
    expect(s.lube).toBe(250)
  })
})

describe('софт-кеи', () => {
  it('на каждом экране ровно SOFTKEY_SLOTS слотов', () => {
    for (const screen of SCREENS) {
      for (const mode of MODES) {
        expect(getSoftkeys({ ...initialState, screen, mode })).toHaveLength(SOFTKEY_SLOTS)
      }
    }
  })

  it('подпись каждого кея — непустая строка', () => {
    for (const screen of SCREENS) {
      for (const key of getSoftkeys({ ...initialState, screen })) {
        if (key) expect(key.label.trim().length).toBeGreaterThan(0)
      }
    }
  })
})

describe('обнуление смещений', () => {
  const dirty: MachineState = {
    ...initialState,
    cursor: { ...initialState.cursor, offset: 2 },
    offsets: initialState.offsets.map((o, i) =>
      i === 2 || i === 3 ? { ...o, values: { ...o.values, X: 11, Y: 22, Z: 33, A: 44 } } : o,
    ),
  }

  it('OFFSET_CLEAR обнуляет только выбранную систему, остальные остаются', () => {
    const s = run(dirty, { type: 'OFFSET_CLEAR' })

    expect(s.offsets[2].values).toEqual({ X: 0, Y: 0, Z: 0, A: 0 })
    expect(s.offsets[3].values).toEqual(dirty.offsets[3].values)
    expect(s.offsets[0].values).toEqual(dirty.offsets[0].values)
    expect(s.message?.text).toContain('обнулено')
  })

  it('привязка A на F5, обнуление выбранной системы на F6', () => {
    const keys = getSoftkeys({ ...dirty, screen: 'offset' })

    expect(keys[4]?.label).toBe('Привязка A')
    expect(keys[5]?.label).toBe(`Обнулить ${dirty.offsets[2].code}`)
    expect(keys[5]?.action).toEqual({ type: 'OFFSET_CLEAR' })
  })
})

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

  it('«Поиск детали» открывает мастер щупа на экране смещений', () => {
    const s = run(manual, { type: 'PROBE_OPEN', open: true })

    expect(s.screen).toBe('offset')
    expect(s.probe.open).toBe(true)
  })

  it('раскладка F1-F8 «Позиция» соответствует плану наладки', () => {
    const keys = getSoftkeys({ ...initialState, screen: 'pos', mode: 'JOG' })

    expect(keys.map((k) => k?.label)).toEqual([
      'Замер инструмента',
      'Поиск детали',
      'СК G54',
      'Безопасный отвод',
      'В ноль детали',
      'Зажим оси A',
      'Вызов инструмента',
      'Калькулятор режимов',
    ])
  })
})

describe('экран «Программа»: поиск, симуляция, запуск', () => {
  const program = (lines: string[]): MachineState => ({
    ...initialState,
    files: [{ name: 'A.NGC', title: 'Тест', lines, modified: '' }],
    activeFile: 0,
    screen: 'prog',
  })

  it('SEARCH_FIND переносит курсор на следующий кадр со строкой', () => {
    const s = run(
      program(['G90 G54', 'T2 M6', 'T5 M6', 'G1 X0 F100']),
      { type: 'SEARCH_QUERY', value: 'T5' },
      { type: 'SEARCH_FIND' },
    )

    expect(s.cursor.prog).toBe(2)
  })

  it('SEARCH_FIND без находки сообщает об ошибке', () => {
    const s = run(program(['G90 G54', 'T2 M6']), { type: 'SEARCH_QUERY', value: 'T9' }, { type: 'SEARCH_FIND' })

    expect(s.cursor.prog).toBe(initialState.cursor.prog)
    expect(s.message?.text).toContain('Не найдено')
  })

  it('SEARCH_REPLACE_ALL заменяет без учёта регистра во всех кадрах', () => {
    const s = run(
      program(['G90 G54', 'T2 M6', 'M6 T2']),
      { type: 'SEARCH_QUERY', value: 't2' },
      { type: 'SEARCH_REPLACE', value: 'T7' },
      { type: 'SEARCH_REPLACE_ALL' },
    )

    expect(currentFile(s).lines).toEqual(['G90 G54', 'T7 M6', 'M6 T7'])
    expect(s.message?.text).toBe('Заменено в кадрах: 2')
  })

  it('SEARCH_REPLACE_ALL вставляет замену буквально, а не как шаблон $', () => {
    const s = run(
      program(['T2 M6']),
      { type: 'SEARCH_QUERY', value: 'T2' },
      { type: 'SEARCH_REPLACE', value: '$&' },
      { type: 'SEARCH_REPLACE_ALL' },
    )

    expect(currentFile(s).lines).toEqual(['$& M6'])
  })

  it('PROG_RENUMBER выстраивает кадры в ряд N10/N20/N30', () => {
    const s = run(program(['G90', 'N7 G1 X0', 'T2 M6']), { type: 'PROG_RENUMBER' })

    expect(currentFile(s).lines).toEqual(['N10 G90', 'N20 G1 X0', 'N30 T2 M6'])
    expect(s.message?.text).toBe('Перенумеровано кадров: 3')
  })

  it('PROG_RENUMBER сохраняет ведущий отступ кадра', () => {
    const s = run(program(['  G90', '  N7 G1 X0']), { type: 'PROG_RENUMBER' })

    expect(currentFile(s).lines).toEqual(['  N10 G90', '  N20 G1 X0'])
    expect(s.message?.text).toBe('Перенумеровано кадров: 2')
  })

  it('SIM: симуляция идёт по кадрам на TICK, оси не двигаются', () => {
    const s0 = run({ ...initialState, screen: 'prog' }, { type: 'SIM' })

    expect(s0.sim.active).toBe(true)
    expect(s0.sim.line).toBe(s0.cursor.prog)

    const s1 = run(s0, { type: 'TICK', dt: 130 }, { type: 'TICK', dt: 130 })

    expect(s1.sim.line).toBe(s0.sim.line + 2)
    expect(s1.run).toBe('IDLE')
    expect(s1.machine).toEqual(initialState.machine)
  })

  it('SIM не запускается во время выполнения', () => {
    const s = run({ ...initialState, screen: 'prog', run: 'RUN' }, { type: 'SIM' })

    expect(s.sim.active).toBe(false)
    expect(s.message?.text).toContain('Остановите')
  })

  it('CYCLE_START гасит симуляцию, чтобы DRO не показывал прогон поверх движения', () => {
    const s = run({ ...initialState, screen: 'prog' }, { type: 'SIM' }, { type: 'CYCLE_START' })

    expect(s.sim.active).toBe(false)
    expect(s.run).toBe('RUN')
  })

  it('PROG_TO_CURSOR восстанавливает модальные G/M-коды из кадров перед стартом', () => {
    const s = run(
      program(['G90 G55 F120 S1200 M3 M8', 'G1 X10 Y10', 'G0 Z5', 'M30']),
      { type: 'CURSOR', delta: 2 },
      { type: 'PROG_TO_CURSOR' },
    )

    expect(s.line).toBe(2)
    expect(s.activeOffset).toBe(1)
    expect(s.spindleSet).toBe(1200)
    expect(s.spindleOn).toBe(true)
    expect(s.coolant).toBe(true)
    expect(s.motion).toBeNull()
    expect(s.message?.text).toContain('G55')
    expect(s.message?.text).toContain('M3')
  })

  it('simPose выдаёт остаток до следующей точки маршрута', () => {
    const segs = buildToolpath(['G1 X10 F100', 'G1 Y10'])

    expect(simPose(segs, 0)).toEqual({ pos: { X: 0, Y: 0, Z: 0 }, dtg: { X: 10, Y: 10, Z: 0 } })
  })

  it('раскладка «Программа»: без дублей физических тумблеров, с поиском/симуляцией/видом', () => {
    const keys = getSoftkeys({ ...initialState, screen: 'prog' })

    expect(keys.map((k) => k?.action?.type)).toEqual([
      'FILE_DIALOG',
      'EDIT_MODE',
      'SEARCH',
      'SIM',
      'PROG_RENUMBER',
      'PROG_REWIND',
      'PROG_TO_CURSOR',
      'TOOLPATH_TOGGLE',
    ])
    expect(keys.some((k) => k?.action?.type === 'TOGGLE')).toBe(false)
  })

  it('при открытом поиске софт-кеи — найти/заменить/закрыть', () => {
    const keys = getSoftkeys({ ...initialState, screen: 'prog', search: { open: true, query: 'T5', replace: '' } })

    expect(keys.map((k) => k?.action?.type).slice(0, 3)).toEqual(['SEARCH_FIND', 'SEARCH_REPLACE_ALL', 'SEARCH'])
  })
})
