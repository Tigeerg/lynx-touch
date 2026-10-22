import {
  INITIAL_ALARMS,
  INITIAL_OFFSETS,
  INITIAL_TOOLS,
  MODE_LABEL,
  PROGRAM_FILES,
  RAPID_JOG_MULT,
  RAPID_STEPS,
  SCREEN_LABEL,
  SOFT_LIMITS,
  WORKPLACE_LABEL,
} from './data'
import { PROBE_CYCLES, PROBE_STEP_MEASURE, PROBE_STEP_RESULT, PROBE_TOOL } from './probe'
import { abortProbe, probeTick } from './probe-exec'
import { initialLadder, isLadderAction, ladderReducer, type LadderAction } from './ladder'
import { idleSim, isProgAction, progReducer, tickSim, type ProgAction } from './prog'
import { parseWords } from './gcode'
import { editorReducer, isEditorAction, type EditorAction } from './editor'
import { isSetupAction, setupReducer, type SetupAction } from './setup'
import { isMpgAction, mpgReducer, type MpgAction } from './mpg'
import { initialTest, isTestAction, testReducer, type TestAction } from './test'
import {
  defaultParams,
  evaluateLadder,
  formatParam,
  sameLadderMem,
  INI_FILE,
  MAX_FEED_PARAM,
  MAX_SPINDLE_PARAM,
  hasServiceList,
  paramAt,
  SERVICE_PASSWORD,
  THEME_OPTIONS,
  THEME_PARAM,
  TOOLPATH_PARAM,
  UI_MODE_PARAM,
  unsavedIds,
  type ParamDef,
} from './service-data'
import {
  AXES,
  MDI_HISTORY_SIZE,
  MODE_LOCKED_SCREENS,
  MODE_START_SCREEN,
  MODES,
  MPG_STEPS,
  SERVICE_SECTIONS,
  screenOfTab,
  tabOfScreen,
  type Axes,
  type Axis,
  type MachineState,
  type Mode,
  type Screen,
  type ServiceSection,
  type Workplace,
} from './types'

import { distanceToGo, motionSpeed, toWork, workOffset, zeroAxes } from './coords'
import { currentFile, programLines } from './files'
import { listLength } from './list'
import { cancelEdit, markSaved, setParam } from './service-edit'
import { notify, pushAlarm } from './messages'
import { MACHINE_LOCK_MSG, blockedFromMotion, isBusy } from './guards'

export { distanceToGo, motionSpeed, toWork, workOffset } from './coords'
export { currentFile, programLines } from './files'

export type Action =
  | { type: 'TICK'; dt: number }
  | { type: 'CYCLE_START' }
  | { type: 'FEED_HOLD' }
  | { type: 'RESET' }
  | { type: 'ESTOP' }
  | { type: 'SET_MODE'; mode: Mode }
  | { type: 'SET_WORKPLACE'; workplace: Workplace }
  | { type: 'SPINDLE_WARMUP' }
  | { type: 'CYCLE_MODE'; dir: 1 | -1 }
  | { type: 'SET_SCREEN'; screen: Screen }
  | { type: 'SELECT_AXIS'; axis: Axis }
  | { type: 'JOG'; dir: 1 | -1; axis?: Axis }
  | { type: 'SET_STEP'; step: number }
  | { type: 'OVERRIDE'; kind: 'feed' | 'spindle' | 'rapid'; dir: 1 | -1 }
  | {
      type: 'TOGGLE'
      key:
        | 'singleBlock'
        | 'optStop'
        | 'dryRun'
        | 'blockSkip'
        | 'machineLock'
        | 'coolant'
        | 'mist'
        | 'workLight'
        | 'chipConveyor'
        | 'jogRapid'
        | 'aBrake'
    }
  | { type: 'SPINDLE_MANUAL'; dir?: 1 | -1 }
  | { type: 'SPINDLE_STOP' }
  | { type: 'TOOL_CLAMP'; clamp: boolean }
  | { type: 'LUBE' }
  | { type: 'GO_SAFE' }
  | { type: 'MDI_BUFFER'; value: string }
  | { type: 'MDI_RECALL'; index: number }
  | { type: 'CURSOR'; delta: number }
  | { type: 'PROG_REWIND' }
  | { type: 'PROG_TO_CURSOR' }
  | { type: 'ALARM_ACK' }
  | { type: 'ALARM_CLEAR_ALL' }
  | { type: 'SET_JOG_FEED'; feed: number }
  | { type: 'HELP'; open?: boolean }
  | { type: 'SERVICE_SECTION'; section: ServiceSection }
  | { type: 'SERVICE_AXIS'; axis: Axis }
  | { type: 'SERVICE_ENTER' }
  | { type: 'SERVICE_CANCEL' }
  | { type: 'SERVICE_BACK' }
  | { type: 'SERVICE_LOCK' }
  | { type: 'SERVICE_AUTH_INPUT'; value: string }
  | { type: 'SERVICE_SAVE' }
  | { type: 'SERVICE_DEFAULTS' }
  | { type: 'PARAM_ADJUST'; dir: 1 | -1; coarse?: boolean }
  | { type: 'THEME_TOGGLE' }
  | { type: 'UI_MODE_SET'; value: 0 | 1 }
  | { type: 'TOOLPATH_TOGGLE' }
  | { type: 'PROBE_OPEN'; open?: boolean }
  | { type: 'PROBE_STEP'; dir: 1 | -1 }
  | { type: 'PROBE_CALL_TOOL' }
  | { type: 'PROBE_WRITE' }
  | { type: 'PROBE_RETRY' }
  | { type: 'CALC'; open?: boolean }
  | ProgAction
  | LadderAction
  | MpgAction
  | TestAction
  | EditorAction
  | SetupAction

export const initialState: MachineState = {
  ladder: initialLadder,
  files: PROGRAM_FILES,
  activeFile: 0,
  fileDialog: false,
  fileCursor: 0,
  toolDialog: false,
  toolBuffer: '',
  calc: false,
  editMode: false,
  editor: null,
  mode: 'AUTO',
  workplace: 'RUN',
  run: 'IDLE',
  screen: MODE_START_SCREEN.AUTO,
  machine: zeroAxes(),
  relOrigin: zeroAxes(),
  offsets: INITIAL_OFFSETS,
  activeOffset: 0,
  tools: INITIAL_TOOLS,
  toolTime: {},
  tool: 1,
  pendingTool: 1,
  line: 0,
  nextLine: 0,
  motion: null,
  dwell: 0,
  programEnd: false,
  stopAfterBlock: false,
  homing: false,
  workZero: false,
  motionModal: 0,
  programFeed: 0,
  spindleSet: 0,
  spindleOn: false,
  spindleDir: 1,
  toolClamped: true,
  aBrake: true,
  coolant: false,
  mist: false,
  lube: 0,
  workLight: false,
  chipConveyor: false,
  mdi: { buffer: '', history: ['G90 G54', 'S1200 M3', 'G0 X0 Y0'], running: false },
  feedOverride: 100,
  spindleOverride: 100,
  rapidOverride: 50,
  singleBlock: false,
  optStop: true,
  dryRun: false,
  blockSkip: false,
  machineLock: false,
  jogAxis: 'X',
  jogStep: 0.1,
  jogRapid: false,
  jogFeed: 1000,
  cursor: { pos: 0, prog: 0, offset: 0, tool: 0, alarm: 0, service: 0 },
  alarms: INITIAL_ALARMS,
  alarmSeq: 10,
  cycleTime: 0,
  lastCycle: 0,
  partCount: 41,
  message: null,
  help: false,
  service: {
    section: 'system',
    focus: 'nav',
    axis: 'X',
    write: false,
    restartPending: false,
    editing: null,
    saved: defaultParams(),
    auth: null,
  },
  probe: {
    open: false,
    cycle: 0,
    step: 0,
    running: false,
    elapsed: 0,
    origin: null,
    result: null,
    written: false,
    prev: null,
  },
  sim: idleSim(),
  test: initialTest,
  search: { open: false, query: '', replace: '' },
  params: defaultParams(),
}

function executeLine(state: MachineState, index: number): MachineState {
  const lines = programLines(state)
  if (index >= lines.length) return { ...state, programEnd: true }
  if (state.blockSkip && lines[index].trimStart().startsWith('/')) {
    return { ...state, line: index, nextLine: index + 1, dwell: 0 }
  }
  return applyBlock({ ...state, line: index, nextLine: index + 1 }, lines[index], index > 0)
}

function applyBlock(state: MachineState, text: string, allowMotion: boolean): MachineState {
  let s: MachineState = { ...state, dwell: 150 }
  const coords: Partial<Axes> = {}
  let home = false
  const mCodes: number[] = []

  for (const { letter, value } of parseWords(text)) {
    if (letter === 'G') {
      if (value === 0 || value === 1) s.motionModal = value
      if (value >= 54 && value <= 59) s.activeOffset = value - 54
      if (value === 28) home = true
    } else if (letter === 'X' || letter === 'Y' || letter === 'Z' || letter === 'A') {
      coords[letter] = value
    } else if (letter === 'F') s.programFeed = value
    else if (letter === 'S') s.spindleSet = value
    else if (letter === 'T') s.pendingTool = value
    else if (letter === 'M') mCodes.push(value)
  }

  if (home) {
    s.motion = { target: { ...s.machine, Z: 0 }, rapid: true, feed: 0 }
  } else if (Object.keys(coords).length > 0 && allowMotion) {
    const off = workOffset(s)
    const work = toWork(s)
    const target = { ...zeroAxes() }
    for (const ax of AXES) target[ax] = (coords[ax] ?? work[ax]) + off[ax]
    s.motion = { target, rapid: s.motionModal === 0, feed: s.programFeed }
  }

  for (const m of mCodes) {
    if (m === 3 || m === 4) {
      s.spindleOn = true
      s.spindleDir = m === 3 ? 1 : -1
    }
    if (m === 5) s.spindleOn = false
    if (m === 6) {
      s.tool = s.pendingTool
      s.dwell = 1800
    }
    if (m === 8) s.coolant = true
    if (m === 9) s.coolant = false
    if (m === 0 || (m === 1 && s.optStop)) {
      s.stopAfterBlock = true
      s = notify(s, m === 0 ? 'Программный останов M00' : 'Опциональный останов M01 — нажмите ПУСК')
    }
    if (m === 30) s.programEnd = true
  }

  if (s.singleBlock) s.stopAfterBlock = true
  return s
}

function stepMotion(s: MachineState, dt: number): MachineState {
  if (!s.motion) return s
  if (s.machineLock) return { ...s, motion: null }
  const { target } = s.motion
  const dist = Math.hypot(...AXES.map((ax) => target[ax] - s.machine[ax]))
  const travel = (motionSpeed(s) / 60000) * dt
  if (dist <= 0.0005 || travel >= dist) return { ...s, machine: { ...target }, motion: null }
  if (travel === 0) return s
  const k = travel / dist
  const machine = { ...s.machine }
  for (const ax of AXES) machine[ax] += (target[ax] - machine[ax]) * k
  return { ...s, machine }
}

function completeBlock(s: MachineState): MachineState {
  if (s.homing) {
    if (s.machine.X !== 0 || s.machine.Y !== 0) {
      return { ...s, motion: { target: { ...s.machine, X: 0, Y: 0 }, rapid: true, feed: 0 } }
    }
    return notify({ ...s, run: 'IDLE', homing: false }, 'Безопасная точка достигнута')
  }
  if (s.mdi.running) {
    return { ...s, run: 'IDLE', mdi: { ...s.mdi, running: false }, stopAfterBlock: false, programEnd: false }
  }
  if (s.workZero) {
    const off = workOffset(s)
    if (Math.abs(s.machine.X - off.X) > 0.0005 || Math.abs(s.machine.Y - off.Y) > 0.0005) {
      return { ...s, motion: { target: { ...s.machine, X: off.X, Y: off.Y, Z: 0 }, rapid: true, feed: 0 } }
    }
    return notify({ ...s, run: 'IDLE', workZero: false }, 'В нуле детали')
  }
  if (s.programEnd) {
    return notify(
      {
        ...s,
        run: 'IDLE',
        programEnd: false,
        line: 0,
        nextLine: 0,
        partCount: s.partCount + 1,
        lastCycle: s.cycleTime,
        cycleTime: 0,
        spindleOn: false,
        coolant: false,
      },
      'Программа завершена (M30). Деталь учтена',
    )
  }
  if (s.stopAfterBlock) return { ...s, run: 'IDLE', stopAfterBlock: false }
  return executeLine(s, s.nextLine)
}

const clamp = (v: number, min: number, max: number) => Math.min(max, Math.max(min, v))

export function reducer(state: MachineState, a: Action): MachineState {
  const s = state
  if (isLadderAction(a)) {
    const r = ladderReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  if (isProgAction(a)) {
    const r = progReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  if (isMpgAction(a)) {
    const r = mpgReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  if (isTestAction(a)) {
    const r = testReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  if (isEditorAction(a)) {
    const r = editorReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  if (isSetupAction(a)) {
    const r = setupReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  switch (a.type) {
    case 'TICK': {
      let n = s
      if (n.message && n.message.ttl > 0) {
        const ttl = n.message.ttl - a.dt
        n = { ...n, message: ttl > 0 ? { ...n.message, ttl } : null }
      }
      if (n.lube > 0) n = { ...n, lube: Math.max(0, n.lube - a.dt) }
      const scan = evaluateLadder(n, undefined, a.dt)
      if (!sameLadderMem(scan.mem, n.ladder.mem)) n = { ...n, ladder: { ...n.ladder, mem: scan.mem } }
      n = tickSim(n, a.dt)
      if (n.run !== 'RUN') return n
      if (n.probe.running) {
        const r = probeTick(n, a.dt)
        return r.msg ? notify(r.s, r.msg) : r.s
      }
      if (n.mode === 'AUTO') n = { ...n, cycleTime: n.cycleTime + a.dt }
      if (n.spindleOn) n = { ...n, toolTime: { ...n.toolTime, [n.tool]: (n.toolTime[n.tool] ?? 0) + a.dt } }
      if (n.dwell > 0) return { ...n, dwell: Math.max(0, n.dwell - a.dt) }
      if (n.motion) return stepMotion(n, a.dt)
      return completeBlock(n)
    }

    case 'CYCLE_START': {
      if (s.run === 'ESTOP') return notify(s, 'Аварийный стоп активен — отпустите кнопку', 'error')
      if (s.run === 'ALARM') return notify(s, 'Есть авария — нажмите СБРОС', 'error')
      if (s.run === 'RUN') return s
      if (s.run === 'HOLD') return notify({ ...s, sim: idleSim(), run: 'RUN' }, 'Подача возобновлена')
      if (s.probe.open && s.screen === 'offset') {
        if (s.probe.step !== PROBE_STEP_MEASURE) return notify(s, 'ПУСК щупа доступен на шаге «Измерение»', 'warn')
        if (s.tool !== PROBE_TOOL) return notify(s, 'В шпинделе нет щупа T99', 'error')
        if (s.spindleOn) return notify(s, 'Остановите шпиндель перед измерением', 'error')
        return notify(
          {
            ...s,
            sim: idleSim(),
            run: 'RUN',
            probe: {
              ...s.probe,
              running: true,
              elapsed: 0,
              origin: { ...s.machine },
              result: null,
              written: false,
              prev: null,
            },
          },
          'Измерение щупом… G38.2',
        )
      }
      if (s.editMode) return notify(s, 'Выйдите из режима редактирования', 'warn')
      if (s.mode === 'AUTO') {
        const start = s.nextLine >= programLines(s).length ? 0 : s.nextLine
        return executeLine({ ...s, sim: idleSim(), run: 'RUN' }, start)
      }
      if (s.workplace === 'SETUP') {
        const text = s.mdi.buffer.trim().toUpperCase()
        if (!text) return notify(s, 'Введите кадр MDI', 'warn')
        if (!s.toolClamped && /M0?[34]\b/.test(text))
          return notify(s, 'Инструмент разжат — зажмите перед пуском шпинделя', 'error')
        const history = [text, ...s.mdi.history.filter((h) => h !== text)].slice(0, MDI_HISTORY_SIZE)
        const n = applyBlock(
          { ...s, sim: idleSim(), run: 'RUN', mdi: { buffer: '', history, running: true } },
          text,
          true,
        )
        return notify({ ...n, programEnd: false, stopAfterBlock: false }, `MDI: ${text}`)
      }
      return notify(s, `ПУСК недоступен в режиме «${WORKPLACE_LABEL[s.workplace]}»`, 'warn')
    }

    case 'FEED_HOLD':
      return s.run === 'RUN' ? notify({ ...s, run: 'HOLD' }, 'Стоп подачи. ПУСК — продолжить', 'warn') : s

    case 'RESET': {
      if (s.run === 'ESTOP') return notify(s, 'Сначала отпустите аварийный стоп', 'error')
      return notify(
        {
          ...abortProbe(s),
          run: 'IDLE',
          motion: null,
          dwell: 0,
          homing: false,
          workZero: false,
          sim: idleSim(),
          programEnd: false,
          stopAfterBlock: false,
          spindleOn: false,
          coolant: false,
          mist: false,
          mdi: { ...s.mdi, running: false },
          line: 0,
          nextLine: 0,
          cycleTime: 0,
          alarms: s.alarms.map((al) => (al.level === 'error' ? { ...al, active: false } : al)),
        },
        'Сброс выполнен',
      )
    }

    case 'ESTOP': {
      if (s.run === 'ESTOP') return notify({ ...s, run: 'ALARM' }, 'Аварийный стоп отпущен — нажмите СБРОС', 'warn')
      const n = pushAlarm(
        {
          ...abortProbe(s),
          run: 'ESTOP',
          motion: null,
          dwell: 0,
          homing: false,
          workZero: false,
          sim: idleSim(),
          spindleOn: false,
          coolant: false,
          mist: false,
          mdi: { ...s.mdi, running: false },
        },
        1001,
        'error',
        'Нажата кнопка аварийного останова',
      )
      return notify(n, 'АВАРИЙНЫЙ СТОП', 'error')
    }

    case 'SET_MODE': {
      if (s.run === 'RUN') return notify(s, 'Смена режима во время выполнения запрещена', 'warn')
      const screen = MODE_START_SCREEN[a.mode]
      const jogStep = a.mode === 'MPG' && !MPG_STEPS.includes(s.jogStep) ? MPG_STEPS[MPG_STEPS.length - 1] : s.jogStep
      const n = s.editMode && screen !== 'prog' ? { ...s, editMode: false, editor: null } : s
      return notify({ ...n, mode: a.mode, screen, jogStep }, `Режим: ${MODE_LABEL[a.mode]}`)
    }

    case 'SET_WORKPLACE': {
      if (isBusy(s)) return notify(s, 'Смена режима во время выполнения запрещена', 'warn')
      const mode: Mode = a.workplace === 'SETUP' ? 'JOG' : 'AUTO'
      const kept = screenOfTab(tabOfScreen(s.screen), a.workplace)
      const screen = MODE_LOCKED_SCREENS[mode].includes(kept) ? screenOfTab('work', a.workplace) : kept
      return notify(
        { ...s, workplace: a.workplace, mode, dryRun: a.workplace === 'TEST', screen },
        `Режим: ${WORKPLACE_LABEL[a.workplace]}`,
      )
    }

    case 'SPINDLE_WARMUP': {
      if (s.run === 'ESTOP' || s.run === 'ALARM') return notify(s, 'Сначала сбросьте аварию', 'error')
      if (isBusy(s)) return notify(s, 'Сначала остановите программу', 'warn')
      if (!s.toolClamped) return notify(s, 'Инструмент разжат — прогрев запрещён', 'error')
      if (s.tool === PROBE_TOOL) return notify(s, 'В шпинделе щуп T99 — прогрев запрещён', 'error')
      const idx = s.files.findIndex((f) => /ПРОГРЕВ/i.test(f.title))
      if (idx < 0) return notify(s, 'Программа прогрева шпинделя не найдена', 'error')
      const base: MachineState = {
        ...s,
        activeFile: idx,
        workplace: 'RUN',
        mode: 'AUTO',
        dryRun: false,
        screen: 'prog',
        cursor: { ...s.cursor, prog: 0 },
      }
      return notify(
        executeLine({ ...base, sim: idleSim(), run: 'RUN', line: 0, nextLine: 0 }, 0),
        'Прогрев шпинделя запущен',
      )
    }

    case 'CYCLE_MODE': {
      const idx = (MODES.indexOf(s.mode) + a.dir + MODES.length) % MODES.length
      return reducer(s, { type: 'SET_MODE', mode: MODES[idx] })
    }

    case 'SET_SCREEN':
      if (MODE_LOCKED_SCREENS[s.mode].includes(a.screen)) {
        return notify(
          s,
          `В режиме «${WORKPLACE_LABEL[s.workplace]}» экран «${SCREEN_LABEL[a.screen]}» заблокирован`,
          'warn',
        )
      }
      if (a.screen !== 'service' && (s.service.editing || s.service.auth)) {
        const reverted = cancelEdit(s)
        return reducer({ ...reverted, service: { ...reverted.service, auth: null } }, a)
      }
      if (a.screen !== 'prog' && s.editMode) {
        return notify({ ...s, screen: a.screen, editMode: false, editor: null }, `Файл ${currentFile(s).name} сохранён`)
      }
      return { ...s, screen: a.screen }

    case 'SELECT_AXIS':
      return { ...s, jogAxis: a.axis }

    case 'JOG': {
      if (s.mode !== 'JOG' && s.mode !== 'MPG')
        return notify(s, 'Перемещение доступно в режиме РУЧНОЙ или МАХОВИК', 'warn')
      if (s.run !== 'IDLE') return notify(s, 'Перемещение заблокировано', 'warn')
      if (s.machineLock) return notify(s, MACHINE_LOCK_MSG, 'warn')
      const ax = a.axis ?? s.jogAxis
      if (ax === 'A' && s.aBrake) return notify(s, 'Тормоз оси A включён — снимите его клавишей F6', 'error')
      const distance = s.jogRapid ? s.jogStep * RAPID_JOG_MULT : s.jogStep
      const next = Math.round((s.machine[ax] + a.dir * distance) * 1000) / 1000
      const [min, max] = SOFT_LIMITS[ax]
      if (next < min || next > max) {
        return notify({ ...s, jogAxis: ax }, `Программный предел оси ${ax}${a.dir > 0 ? '+' : '−'}`, 'error')
      }
      return { ...s, jogAxis: ax, machine: { ...s.machine, [ax]: next } }
    }

    case 'SET_STEP':
      if (s.mode !== 'JOG' && s.mode !== 'MPG') return notify(s, 'Шаг выбирается в режиме РУЧНОЙ или МАХОВИК', 'warn')
      if (s.mode === 'MPG' && !MPG_STEPS.includes(a.step))
        return notify(s, 'Маховик: доступны множители ×1, ×10, ×100', 'warn')
      return { ...s, jogStep: a.step }

    case 'SET_JOG_FEED':
      if (s.mode !== 'JOG') return notify(s, 'Скорость перемещения задаётся в режиме РУЧНОЙ', 'warn')
      return { ...s, jogFeed: a.feed }

    case 'OVERRIDE': {
      if (a.kind === 'feed')
        return { ...s, feedOverride: clamp(s.feedOverride + a.dir * 10, 0, s.params[MAX_FEED_PARAM]) }
      if (a.kind === 'spindle')
        return { ...s, spindleOverride: clamp(s.spindleOverride + a.dir * 10, 50, s.params[MAX_SPINDLE_PARAM]) }
      const idx = clamp(RAPID_STEPS.indexOf(s.rapidOverride) + a.dir, 0, RAPID_STEPS.length - 1)
      return { ...s, rapidOverride: RAPID_STEPS[idx] }
    }

    case 'TOGGLE':
      if (a.key === 'dryRun' && isBusy(s)) return notify(s, 'Холостой ход переключается только в останове', 'warn')
      if (a.key === 'machineLock' && isBusy(s))
        return notify(s, 'Блокировка станка переключается только в останове', 'warn')
      return { ...s, [a.key]: !s[a.key] }

    case 'SPINDLE_STOP':
      if (!s.spindleOn) return s
      if (isBusy(s)) return notify(s, 'Остановка шпинделя во время цикла недоступна', 'warn')
      return notify({ ...s, spindleOn: false }, 'Шпиндель остановлен (M5)')

    case 'SPINDLE_MANUAL': {
      if (s.mode === 'AUTO' || isBusy(s) || s.run === 'ESTOP') {
        return notify(s, 'Ручное управление шпинделем недоступно', 'warn')
      }
      const dir = a.dir ?? s.spindleDir
      if (s.spindleOn) {
        if (dir !== s.spindleDir) return notify(s, 'Сначала остановите шпиндель', 'warn')
        return notify({ ...s, spindleOn: false }, 'Шпиндель остановлен')
      }
      if (!s.toolClamped) return notify(s, 'Инструмент разжат — пуск шпинделя запрещён', 'error')
      if (s.tool === PROBE_TOOL) return notify(s, 'В шпинделе щуп T99 — вращение запрещено', 'error')
      return notify(
        { ...s, spindleOn: true, spindleDir: dir, spindleSet: s.spindleSet || 1000 },
        dir > 0 ? 'Шпиндель по часовой (M3)' : 'Шпиндель против часовой (M4)',
      )
    }

    case 'TOOL_CLAMP':
      if (a.clamp === s.toolClamped) return s
      if (!a.clamp && (s.spindleOn || isBusy(s)))
        return notify(s, 'Разжим запрещён: шпиндель вращается или идёт цикл', 'error')
      return notify(
        { ...s, toolClamped: a.clamp },
        a.clamp ? 'Инструмент зажат' : 'Инструмент разжат',
        a.clamp ? 'info' : 'warn',
      )

    case 'LUBE':
      if (s.run === 'ESTOP') return notify(s, 'Смазка недоступна при аварийном стопе', 'warn')
      return notify({ ...s, lube: 3000 }, 'Принудительная смазка направляющих')

    case 'GO_SAFE':
      if (s.run === 'ESTOP' || s.run === 'ALARM') return notify(s, 'Сначала сбросьте аварию', 'error')
      return (
        blockedFromMotion(s, 'Остановите программу перед выходом в безопасную точку') ??
        notify(
          {
            ...s,
            run: 'RUN',
            homing: true,
            workZero: false,
            motion: { target: { ...s.machine, Z: 0 }, rapid: true, feed: 0 },
          },
          'Выход в безопасную точку: Z0, затем X0 Y0',
        )
      )

    case 'MDI_BUFFER':
      return { ...s, mdi: { ...s.mdi, buffer: a.value.toUpperCase() } }

    case 'MDI_RECALL': {
      const text = s.mdi.history[a.index]
      return text ? { ...s, mdi: { ...s.mdi, buffer: text } } : s
    }

    case 'SERVICE_SECTION': {
      const base = cancelEdit(s)
      return {
        ...base,
        service: { ...base.service, section: a.section, focus: 'nav' },
        cursor: { ...base.cursor, service: 0 },
      }
    }

    case 'SERVICE_AXIS':
      if (s.service.editing || s.service.auth) return s
      return { ...s, service: { ...s.service, axis: a.axis } }

    case 'SERVICE_ENTER': {
      const sv = s.service
      if (sv.auth) {
        if (sv.auth.input === SERVICE_PASSWORD) {
          return notify({ ...s, service: { ...sv, auth: null, write: true } }, 'Запись параметров разрешена', 'warn')
        }
        return { ...s, service: { ...sv, auth: { input: '', error: true } } }
      }
      if (sv.editing) {
        const def = paramAt(sv.section, s.cursor.service, sv.axis)
        const committed: MachineState = { ...s, service: { ...sv, editing: null } }
        return def?.free ? markSaved(committed, def.id, s.params[def.id]) : committed
      }
      if (sv.focus === 'nav') {
        if (!hasServiceList(sv.section)) return notify(s, 'В этом разделе нет редактируемых параметров')
        return { ...s, service: { ...sv, focus: 'list' } }
      }
      if (sv.section === 'ladder') return reducer(s, { type: 'LADDER_EDIT', on: true })
      const def = paramAt(sv.section, s.cursor.service, sv.axis)
      if (!def) return s
      if (!def.free) {
        if (isBusy(s)) return notify(s, 'Изменение параметров во время цикла запрещено', 'warn')
        if (!sv.write) return { ...s, service: { ...sv, auth: { input: '', error: false } } }
      }
      return { ...s, service: { ...sv, editing: { id: def.id, original: s.params[def.id] } } }
    }

    case 'SERVICE_CANCEL': {
      const sv = s.service
      if (sv.auth) return { ...s, service: { ...sv, auth: null } }
      if (sv.editing) return notify(cancelEdit(s), 'Изменение отменено')
      if (sv.focus === 'list') return { ...s, service: { ...sv, focus: 'nav' } }
      return s
    }

    case 'SERVICE_BACK': {
      const sv = s.service
      if (sv.auth || sv.editing || sv.focus === 'list') return reducer(s, { type: 'SERVICE_CANCEL' })
      if (sv.section !== SERVICE_SECTIONS[0])
        return reducer(s, { type: 'SERVICE_SECTION', section: SERVICE_SECTIONS[0] })
      return reducer(s, { type: 'SET_SCREEN', screen: 'pos' })
    }

    case 'SERVICE_LOCK': {
      const sv = s.service
      if (sv.auth) return s
      if (sv.write) {
        if (sv.editing || unsavedIds(s.params, sv.saved).length > 0) {
          return notify(s, 'Есть несохранённые изменения — сначала нажмите «Сохранить»', 'warn')
        }
        return notify({ ...s, service: { ...sv, write: false } }, 'Запись параметров заблокирована')
      }
      if (isBusy(s)) return notify(s, 'Запись параметров недоступна во время цикла', 'warn')
      return { ...s, service: { ...sv, auth: { input: '', error: false } } }
    }

    case 'SERVICE_AUTH_INPUT':
      if (!s.service.auth) return s
      return { ...s, service: { ...s.service, auth: { input: a.value.slice(0, 12), error: false } } }

    case 'SERVICE_SAVE': {
      const sv = s.service
      if (!sv.write) return notify(s, 'Запись заблокирована — разрешите запись (F2)', 'warn')
      if (sv.editing) return notify(s, 'Завершите правку параметра: Ввод или Отмена', 'warn')
      if (isBusy(s)) return notify(s, 'Сохранение недоступно во время цикла', 'warn')
      const count = unsavedIds(s.params, sv.saved).length
      if (count === 0) return notify(s, 'Нет несохранённых изменений')
      return notify(
        { ...s, service: { ...sv, saved: { ...s.params } } },
        sv.restartPending
          ? `Сохранено в ${INI_FILE}: ${count}. Требуется перезапуск LinuxCNC`
          : `Сохранено в ${INI_FILE}: ${count}`,
        sv.restartPending ? 'warn' : 'info',
      )
    }

    case 'SERVICE_DEFAULTS': {
      const sv = s.service
      if (sv.auth || sv.editing || sv.focus !== 'list') return s
      const def = paramAt(sv.section, s.cursor.service, sv.axis)
      if (!def) return s
      const value = defaultParams()[def.id]
      if (s.params[def.id] === value) return notify(s, 'Параметр уже имеет значение по умолчанию')
      if (!def.free) {
        if (isBusy(s)) return notify(s, 'Изменение параметров во время цикла запрещено', 'warn')
        if (!sv.write) return notify(s, 'Параметр защищён — разрешите запись (F2)', 'warn')
      }
      let n = setParam(s, def, value)
      if (def.free) n = markSaved(n, def.id, value)
      return notify(
        n,
        def.free ? 'Сброшено по умолчанию' : 'Сброшено по умолчанию. Нажмите «Сохранить», чтобы записать',
      )
    }

    case 'PARAM_ADJUST': {
      const sv = s.service
      if (sv.section === 'ladder') return sv.focus === 'list' ? reducer(s, { type: 'LADDER_COL', dir: a.dir }) : s
      if (sv.auth) return s
      if (!sv.editing) {
        if (sv.focus === 'list' && (sv.section === 'joints' || sv.section === 'pid')) {
          const axis = AXES[(AXES.indexOf(sv.axis) + a.dir + AXES.length) % AXES.length]
          return { ...s, service: { ...sv, axis } }
        }
        return s
      }
      const def = paramAt(sv.section, s.cursor.service, sv.axis)
      if (!def || def.id !== sv.editing.id) return s
      const cur = s.params[def.id]
      let next: number
      if (def.options) {
        next = (cur + a.dir + def.options.length) % def.options.length
      } else {
        const k = 10 ** Math.max(def.digits, 4)
        next = Math.round(clamp(cur + a.dir * def.step * (a.coarse ? 10 : 1), def.min, def.max) * k) / k
      }
      return next === cur ? s : setParam(s, def, next)
    }

    case 'THEME_TOGGLE':
      return markSaved(
        { ...s, params: { ...s.params, [THEME_PARAM]: (s.params[THEME_PARAM] + 1) % THEME_OPTIONS.length } },
        THEME_PARAM,
        (s.params[THEME_PARAM] + 1) % THEME_OPTIONS.length,
      )
    case 'UI_MODE_SET':
      if (s.params[UI_MODE_PARAM] === a.value) return s
      return markSaved({ ...s, params: { ...s.params, [UI_MODE_PARAM]: a.value } }, UI_MODE_PARAM, a.value)
    case 'TOOLPATH_TOGGLE': {
      const show = s.params[TOOLPATH_PARAM] ? 0 : 1
      return notify(
        markSaved({ ...s, params: { ...s.params, [TOOLPATH_PARAM]: show } }, TOOLPATH_PARAM, show),
        show ? 'Предпросмотр траектории включён' : 'Предпросмотр траектории скрыт',
      )
    }

    case 'PROBE_OPEN': {
      const open = a.open ?? !s.probe.open
      if (!open && s.probe.running) return notify(s, 'Дождитесь окончания измерения или нажмите СБРОС', 'warn')
      if (open && isBusy(s)) return notify(s, 'Остановите программу', 'warn')
      if (open && MODE_LOCKED_SCREENS[s.mode].includes('offset'))
        return notify(s, `Щуп недоступен в режиме «${WORKPLACE_LABEL[s.workplace]}»`, 'warn')
      return {
        ...s,
        screen: open ? 'offset' : s.screen,
        probe: { ...s.probe, open, step: open ? 0 : s.probe.step, result: open ? null : s.probe.result },
      }
    }

    case 'PROBE_STEP': {
      if (s.probe.running) return s
      const step = clamp(s.probe.step + a.dir, 0, PROBE_STEP_MEASURE)
      if (a.dir > 0 && s.probe.step === 1 && s.tool !== PROBE_TOOL)
        return notify(s, 'Установите щуп T99 в шпиндель (F3)', 'warn')
      if (a.dir > 0 && s.probe.step === PROBE_STEP_MEASURE) return notify(s, 'Нажмите ПУСК для измерения', 'warn')
      const from = s.probe.step === PROBE_STEP_RESULT && a.dir < 0 ? PROBE_STEP_MEASURE : step
      return { ...s, probe: { ...s.probe, step: from } }
    }

    case 'PROBE_CALL_TOOL':
      if (isBusy(s)) return notify(s, 'Остановите программу', 'warn')
      return notify(
        { ...s, tool: PROBE_TOOL, pendingTool: PROBE_TOOL, spindleOn: false },
        'Щуп T99 в шпинделе. Шпиндель заблокирован',
      )

    case 'PROBE_WRITE': {
      const r = s.probe.result
      if (!r) return s
      const i = s.cursor.offset
      const offsets = s.offsets.map((o, idx) => (idx === i ? { ...o, values: { ...o.values, ...r.values } } : o))
      const axes = Object.keys(r.values).join(' ')
      const prev = s.probe.prev ?? { ...s.offsets[i].values }
      return notify(
        { ...s, offsets, probe: { ...s.probe, written: true, prev } },
        `Записано в ${s.offsets[i].code}: ${axes}`,
      )
    }

    case 'PROBE_RETRY':
      return {
        ...s,
        probe: { ...s.probe, step: PROBE_STEP_MEASURE, result: null, written: false, prev: null, elapsed: 0 },
      }

    case 'CALC':
      return { ...s, calc: a.open ?? !s.calc }

    case 'CURSOR': {
      if (s.screen === 'offset' && s.probe.open && s.probe.step === 0) {
        return {
          ...s,
          probe: { ...s.probe, cycle: clamp(s.probe.cycle + Math.sign(a.delta), 0, PROBE_CYCLES.length - 1) },
        }
      }
      if (s.screen === 'service' && !s.help) {
        const sv = s.service
        if (sv.auth) return s
        if (sv.editing) return reducer(s, { type: 'PARAM_ADJUST', dir: a.delta < 0 ? 1 : -1, coarse: true })
        if (sv.focus === 'nav') {
          const i = SERVICE_SECTIONS.indexOf(sv.section)
          return reducer(s, {
            type: 'SERVICE_SECTION',
            section: SERVICE_SECTIONS[clamp(i + a.delta, 0, SERVICE_SECTIONS.length - 1)],
          })
        }
      }
      if (s.screen === 'service' && s.service.section === 'ladder') {
        if (s.ladder.form) return s
        if (s.ladder.manager) return reducer(s, { type: 'LADDER_PICK_MOVE', delta: a.delta })
      }
      const len = listLength(s, s.screen)
      return { ...s, cursor: { ...s.cursor, [s.screen]: clamp(s.cursor[s.screen] + a.delta, 0, len - 1) } }
    }

    case 'PROG_REWIND':
      if (isBusy(s)) return notify(s, 'Остановите программу', 'warn')
      return notify(
        { ...s, line: 0, nextLine: 0, sim: idleSim(), cursor: { ...s.cursor, prog: 0 } },
        'Программа в начале',
      )

    case 'PROG_TO_CURSOR': {
      if (isBusy(s)) return notify(s, 'Остановите программу', 'warn')
      const idx = s.cursor.prog
      let base: MachineState = {
        ...s,
        sim: idleSim(),
        motion: null,
        dwell: 0,
        homing: false,
        stopAfterBlock: false,
        programEnd: false,
        message: null,
      }
      for (let i = 0; i < idx; i++) base = applyBlock(base, programLines(s)[i], false)
      const codes = [base.offsets[base.activeOffset].code]
      if (base.spindleOn) codes.push(`S${base.spindleSet} ${base.spindleDir === 1 ? 'M3' : 'M4'}`)
      if (base.coolant) codes.push('M8')
      return notify(
        {
          ...base,
          line: idx,
          nextLine: idx,
          dwell: 0,
          motion: null,
          homing: false,
          stopAfterBlock: false,
          programEnd: false,
          message: null,
        },
        `Старт с кадра ${idx + 1}: восстановлено ${codes.join(', ')}`,
      )
    }

    case 'ALARM_ACK': {
      const i = s.cursor.alarm
      if (!s.alarms[i]) return s
      return { ...s, alarms: s.alarms.map((al, idx) => (idx === i ? { ...al, active: false } : al)) }
    }

    case 'ALARM_CLEAR_ALL': {
      const alarms = s.alarms.filter((al) => al.active)
      return { ...s, alarms, cursor: { ...s.cursor, alarm: clamp(s.cursor.alarm, 0, Math.max(0, alarms.length - 1)) } }
    }

    case 'HELP':
      return { ...s, help: a.open ?? !s.help }

    default:
      return s
  }
}
