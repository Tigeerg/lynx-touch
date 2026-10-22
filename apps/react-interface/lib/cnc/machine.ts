import { clampIndex } from './clamp'
import { controlReducer, isControlAction, type ControlAction } from './control'
import { zeroAxes } from './coords'
import { cycleReducer, isCycleAction, type CycleAction } from './cycle'
import { INITIAL_ALARMS, INITIAL_OFFSETS, INITIAL_TOOLS, PROGRAM_FILES } from './data'
import { editorReducer, isEditorAction, type EditorAction } from './editor'
import { initialLadder, isLadderAction, ladderReducer, type LadderAction } from './ladder'
import { notify } from './messages'
import { isMpgAction, mpgReducer, type MpgAction } from './mpg'
import { isNavAction, navReducer, type NavAction } from './nav'
import { isProbeWizardAction, probeWizardReducer, type ProbeWizardAction } from './probe-wizard'
import { idleSim, isProgAction, progReducer, type ProgAction } from './prog'
import { isServiceAction, serviceReducer, type ServiceAction } from './service'
import { defaultParams, THEME_OPTIONS, THEME_PARAM, TOOLPATH_PARAM, UI_MODE_PARAM } from './service-data'
import { markSaved } from './service-edit'
import { isSetupAction, setupReducer, type SetupAction } from './setup'
import { initialTest, isTestAction, testReducer, type TestAction } from './test'
import { MODE_START_SCREEN, type MachineState } from './types'

export { distanceToGo, motionSpeed, toWork, workOffset } from './coords'
export { currentFile, programLines } from './files'

export type Action =
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
  | NavAction

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
  if (isNavAction(a)) {
    const r = navReducer(s, a, reducer)
    return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  switch (a.type) {
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

    case 'ALARM_ACK': {
      const i = s.cursor.alarm
      if (!s.alarms[i]) return s
      return { ...s, alarms: s.alarms.map((al, idx) => (idx === i ? { ...al, active: false } : al)) }
    }

    case 'ALARM_CLEAR_ALL': {
      const alarms = s.alarms.filter((al) => al.active)
      return { ...s, alarms, cursor: { ...s.cursor, alarm: clampIndex(s.cursor.alarm, 0, alarms.length) } }
    }

    case 'HELP':
      return { ...s, help: a.open ?? !s.help }

    default:
      return s
  }
}
