import {
  INITIAL_ALARMS,
  INITIAL_OFFSETS,
  INITIAL_TOOLS,
  MODE_LABEL,
  PROGRAM_FILES,
  SCREEN_LABEL,
  WORKPLACE_LABEL,
} from './data'
import { PROBE_CYCLES, PROBE_TOOL } from './probe'
import { applyBlock, cycleReducer, executeLine, isCycleAction, type CycleAction } from './cycle'
import { initialLadder, isLadderAction, ladderReducer, type LadderAction } from './ladder'
import { idleSim, isProgAction, progReducer, type ProgAction } from './prog'
import { editorReducer, isEditorAction, type EditorAction } from './editor'
import { isSetupAction, setupReducer, type SetupAction } from './setup'
import { controlReducer, isControlAction, type ControlAction } from './control'
import { isProbeWizardAction, probeWizardReducer, type ProbeWizardAction } from './probe-wizard'
import { isServiceAction, serviceReducer, type ServiceAction } from './service'
import { isMpgAction, mpgReducer, type MpgAction } from './mpg'
import { initialTest, isTestAction, testReducer, type TestAction } from './test'
import {
  defaultParams,
  formatParam,
  THEME_OPTIONS,
  THEME_PARAM,
  TOOLPATH_PARAM,
  UI_MODE_PARAM,
  type ParamDef,
} from './service-data'
import {
  MODE_LOCKED_SCREENS,
  MODE_START_SCREEN,
  MODES,
  MPG_STEPS,
  SERVICE_SECTIONS,
  screenOfTab,
  tabOfScreen,
  type Axis,
  type MachineState,
  type Mode,
  type Screen,
  type Workplace,
} from './types'

import { zeroAxes } from './coords'
import { currentFile, programLines } from './files'
import { listLength } from './list'
import { cancelEdit, markSaved } from './service-edit'
import { notify } from './messages'
import { isBusy } from './guards'

export { distanceToGo, motionSpeed, toWork, workOffset } from './coords'
export { currentFile, programLines } from './files'

export type Action =
  | { type: 'SET_MODE'; mode: Mode }
  | { type: 'SET_WORKPLACE'; workplace: Workplace }
  | { type: 'SPINDLE_WARMUP' }
  | { type: 'CYCLE_MODE'; dir: 1 | -1 }
  | { type: 'SET_SCREEN'; screen: Screen }
  | { type: 'CURSOR'; delta: number }
  | { type: 'PROG_REWIND' }
  | { type: 'PROG_TO_CURSOR' }
  | { type: 'ALARM_ACK' }
  | { type: 'ALARM_CLEAR_ALL' }
  | { type: 'HELP'; open?: boolean }
  | { type: 'THEME_TOGGLE' }
  | { type: 'UI_MODE_SET'; value: 0 | 1 }
  | { type: 'TOOLPATH_TOGGLE' }
  | { type: 'CALC'; open?: boolean }
  | ProgAction
  | LadderAction
  | MpgAction
  | TestAction
  | EditorAction
  | SetupAction
  | ControlAction
  | ProbeWizardAction
  | ServiceAction
  | CycleAction

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
  if (isControlAction(a)) {
    const r = controlReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  if (isProbeWizardAction(a)) {
    const r = probeWizardReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  if (isServiceAction(a)) {
    const r = serviceReducer(s, a, reducer)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  if (isCycleAction(a)) {
    const r = cycleReducer(s, a)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  switch (a.type) {
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
