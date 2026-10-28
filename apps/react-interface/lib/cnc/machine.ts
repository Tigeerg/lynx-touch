import { controlReducer, isControlAction } from './control'
import { zeroAxes } from './coords'
import { cycleReducer, isCycleAction } from './cycle'
import { INITIAL_ALARMS, INITIAL_OFFSETS, INITIAL_TOOLS, PROGRAM_FILES } from './data'
import { editorReducer, isEditorAction } from './editor'
import { initialLadder, isLadderAction, ladderReducer } from './ladder'
import { machineReducer } from './machine-branches'
import { notify } from './messages'
import { isMpgAction, mpgReducer } from './mpg'
import { isNavAction, navReducer } from './nav'
import { isProbeWizardAction, probeWizardReducer } from './probe-wizard'
import { idleSim, isProgAction, progReducer } from './prog'
import { isServiceAction, serviceReducer } from './service'
import { defaultParams } from './service-data'
import { isSetupAction, setupReducer } from './setup'
import { defineSubsystem, isMachineAction, type Action, type MachineAction, type Subsystem } from './subsystem'
import { initialTest, isTestAction, testReducer } from './test'
import { initialToolMeasure, isToolMeasureAction, toolMeasureReducer } from './tool-measure'
import { MODE_START_SCREEN, type MachineState } from './types'

export { distanceToGo, motionSpeed, toWork, workOffset } from './coords'
export { currentFile, programLines } from './files'
export type { Action } from './subsystem'

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
  airBlast: false,
  wash: false,
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
  toolMeasure: initialToolMeasure,
  search: { open: false, query: '', replace: '' },
  params: defaultParams(),
}

// The subsystems the machine composes, in the order they claim their actions: a subsystem is
// registered here and nowhere else, so joining the machine is a line, not another `if`
// (CLAUDE.md, section 6.8). subsystem.ts holds the shape of an entry and the action union.
const SUBSYSTEMS: Subsystem[] = [
  defineSubsystem({ claims: isLadderAction, reduce: ladderReducer }),
  defineSubsystem({ claims: isProgAction, reduce: progReducer }),
  defineSubsystem({ claims: isMpgAction, reduce: mpgReducer }),
  defineSubsystem({ claims: isTestAction, reduce: testReducer }),
  defineSubsystem({ claims: isToolMeasureAction, reduce: toolMeasureReducer }),
  defineSubsystem({ claims: isEditorAction, reduce: editorReducer }),
  defineSubsystem({ claims: isSetupAction, reduce: setupReducer }),
  defineSubsystem({ claims: isControlAction, reduce: controlReducer }),
  defineSubsystem({ claims: isProbeWizardAction, reduce: probeWizardReducer }),
  defineSubsystem({ claims: isServiceAction, reduce: serviceReducer }),
  defineSubsystem({ claims: isCycleAction, reduce: cycleReducer }),
  defineSubsystem({ claims: isNavAction, reduce: navReducer }),
]

export function reducer(state: MachineState, a: Action): MachineState {
  for (const subsystem of SUBSYSTEMS) {
    const r = subsystem.reduce(state, a, reducer)
    if (r) return r.msg ? notify(r.s, r.msg, r.level) : r.s
  }
  return isMachineAction(a) ? machineReducer(state, a) : state
}
