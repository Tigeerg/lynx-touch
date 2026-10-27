import type { PathView } from './toolpath'

export type Axis = 'X' | 'Y' | 'Z' | 'A'
export const AXES: readonly Axis[] = Object.freeze(['X', 'Y', 'Z', 'A'])
export type Axes = Record<Axis, number>

export type Mode = 'AUTO' | 'MDI' | 'JOG' | 'MPG'
export const MODES: readonly Mode[] = Object.freeze(['AUTO', 'MDI', 'JOG', 'MPG'])

// Operator workplace: the top level of the panel. It defines the set of softkeys and the
// screen layout, independently of the machine mode (Mode) and the screen (Screen).
export type Workplace = 'SETUP' | 'TEST' | 'RUN'
// The order sets the order of the buttons on the console and in the header (right to left): RUN, TEST, SETUP.
export const WORKPLACES: Workplace[] = ['RUN', 'TEST', 'SETUP']

// Screen tabs: the Work tab merges the former Position and Program; the workplace chooses its
// content (Setup / PROGON / Auto).
export type Tab = 'work' | 'offset' | 'tool' | 'alarm' | 'service'
export const TABS: readonly Tab[] = Object.freeze(['work', 'offset', 'tool', 'alarm', 'service'])
export const TAB_LABEL: Record<Tab, string> = {
  work: 'Работа',
  offset: 'Смещения',
  tool: 'Инструмент',
  alarm: 'Сообщения',
  service: 'Сервис',
}
export const tabOfScreen = (screen: Screen): Tab => (screen === 'pos' || screen === 'prog' ? 'work' : screen)
export const screenOfTab = (tab: Tab, workplace: Workplace): Screen =>
  tab === 'work' ? (workplace === 'SETUP' ? 'pos' : 'prog') : tab

export type RunState = 'IDLE' | 'RUN' | 'HOLD' | 'ALARM' | 'ESTOP'

export type Screen = 'pos' | 'prog' | 'offset' | 'tool' | 'alarm' | 'service'
export const SCREENS: readonly Screen[] = Object.freeze(['pos', 'prog', 'offset', 'tool', 'alarm', 'service'])

export const MODE_START_SCREEN: Record<Mode, Screen> = { AUTO: 'prog', MDI: 'pos', JOG: 'pos', MPG: 'pos' }

export const MPG_STEPS: readonly number[] = Object.freeze([0.001, 0.01, 0.1])
export const mpgMultiplier = (step: number) => Math.round(step / 0.001)
export const JOG_FEEDS: readonly number[] = Object.freeze([100, 500, 1000, 3000])
export const MDI_HISTORY_SIZE = 10

// The service sections split by what they hold: a catalogue row each (ParamSection), a list
// without catalogue rows (MonitorSection: the HAL monitor and the ladder scheme), or nothing to
// list (HelpSection). The type carries the rule, so paramAt('hal', ...) does not compile instead
// of quietly returning an empty row (CLAUDE.md, section 4).
export type ParamSection = 'system' | 'linuxcnc' | 'motion' | 'joints' | 'pid' | 'mesa'
export type MonitorSection = 'hal' | 'ladder'
export type HelpSection = 'help'
export type ServiceSection = ParamSection | MonitorSection | HelpSection

const PARAM_SECTIONS = new Set<ServiceSection>(['system', 'linuxcnc', 'motion', 'joints', 'pid', 'mesa'])
export const isParamSection = (section: ServiceSection): section is ParamSection => PARAM_SECTIONS.has(section)

export const SERVICE_PAGES: ServiceSection[][] = [
  ['system', 'linuxcnc', 'motion', 'joints', 'pid'],
  ['mesa', 'hal', 'ladder', 'help'],
]
export const SERVICE_PAGE_LABEL = ['ЧПУ LinuxCNC', 'Оборудование']
export const SERVICE_SECTIONS: ServiceSection[] = SERVICE_PAGES.flat()

export type ServiceFocus = 'nav' | 'list'

export interface SimState {
  active: boolean
  line: number
  timer: number
}

// The PROGON run modes: the simulation walks the blocks without moving the axes; the dry run
// executes them with dryRun on (a slow feed, the axes do move) - the way the PROGON used to
// start. A named union, not a flag: a third mode must be added here, not guessed at call sites.
export type TestRunMode = 'sim' | 'dry'

// Workpiece PROGON (test) state: the NC bounding box, the view angle, and the run mode the
// ПУСК uses.
export interface TestState {
  /** The NC bounding box is shown (F5). */
  bbox: boolean
  /** Work: what to show on the screen (F8 "View"). */
  autoView: AutoView
  view: PathView
  /** What ПУСК runs: the axis-free simulation or the dry run (F4 cycles it). */
  runMode: TestRunMode
}

// Auto view modes, cycled by F8: toolpath, gcode, both, and the code over the toolpath.
export type AutoView = 'trajectory' | 'gcode' | 'both' | 'overlay'
export const AUTO_VIEWS: AutoView[] = ['trajectory', 'gcode', 'both', 'overlay']

// Tool-length measurement (the tool setter): a modal run with animated phases.
export interface ToolMeasureState {
  open: boolean
  running: boolean
  /** Seconds elapsed in the run (MEASURE_TOTAL_S is the whole run). */
  elapsed: number
  /** The measured length, mm; null until the run finishes. */
  length: number | null
}

export interface SearchState {
  open: boolean
  query: string
  replace: string
}

export interface ServiceState {
  section: ServiceSection
  focus: ServiceFocus
  axis: Axis
  write: boolean
  restartPending: boolean
  editing: { id: string; original: number } | null
  saved: Record<string, number>
  auth: { input: string; error: boolean } | null
}

export interface ProbeState {
  open: boolean
  cycle: number
  step: number
  running: boolean
  elapsed: number
  origin: Axes | null
  result: { values: Partial<Axes>; info: { label: string; value: string }[] } | null
  written: boolean
  prev: Partial<Axes> | null
}

export interface WorkOffset {
  code: string
  values: Axes
}

export interface Tool {
  number: number
  name: string
  diameter: number
  length: number
  life: number
}

export type AlarmLevel = 'error' | 'warning' | 'info'

export interface Alarm {
  id: string
  code: number
  level: AlarmLevel
  text: string
  time: string
  active: boolean
}

export interface Motion {
  target: Axes
  rapid: boolean
  feed: number
}

export interface Message {
  text: string
  level: 'info' | 'warn' | 'error'
  ttl: number
}

export interface ProgramFile {
  name: string
  title: string
  lines: string[]
  modified: string
}

export interface LineEditor {
  line: number
  buffer: string
  inserted: boolean
}

export type CompareOp = '>' | '<' | '>=' | '<=' | '=' | '<>'

export interface LadderCompare {
  op: CompareOp
  a: string
  b: string
}

export interface LadderContact {
  addr: string
  label: string
  nc?: boolean
  /** When set, the element is a comparison block, not a contact. */
  cmp?: LadderCompare
}

export type CoilKind = 'coil' | 'set' | 'reset' | 'ton' | 'tof' | 'ctu' | 'ctd'

export interface LadderCoil extends LadderContact {
  kind?: CoilKind
  /** Timer preset, ms (ton/tof). Missing means DEFAULT_PRESET_MS. */
  presetMs?: number
  /** Counter preset, pulses (ctu/ctd). Missing means DEFAULT_PRESET_COUNT. */
  presetCount?: number
}

export interface LadderRung {
  title: string
  contacts: LadderContact[]
  coil: LadderCoil
  /** Parallel branches (OR): each branch is a sequence of contacts (AND). */
  or?: LadderContact[][]
}

export interface LadderMem {
  timers: Record<string, { acc: number; q: boolean }>
  counters: Record<string, { acc: number; q: boolean; prev: boolean }>
  latched: Record<string, boolean>
}

export interface LadderScheme {
  name: string
  title: string
  rungs: LadderRung[]
  modified: string
}

export type LadderForm =
  | { kind: 'element'; values: [string, string, string, string]; compare?: boolean }
  | { kind: 'scheme'; target: number; values: [string, string] }

export interface LadderState {
  schemes: LadderScheme[]
  active: number
  draft: LadderRung[] | null
  dirty: boolean
  col: number
  /** Branch index: 0 is the main line, 1.. are the parallel (OR) branches. */
  path: number
  manager: boolean
  pick: number
  form: LadderForm | null
  mem: LadderMem
  /** The signal map of the last PLC scan (TICK), so the monitor reads it instead of rescanning. */
  signals: Record<string, boolean>
}

export interface MachineState {
  ladder: LadderState
  files: ProgramFile[]
  activeFile: number
  fileDialog: boolean
  fileCursor: number
  toolDialog: boolean
  toolBuffer: string
  calc: boolean
  editMode: boolean
  editor: LineEditor | null
  mode: Mode
  workplace: Workplace
  run: RunState
  screen: Screen
  machine: Axes
  relOrigin: Axes
  offsets: WorkOffset[]
  activeOffset: number
  tools: Tool[]
  toolTime: Record<number, number>
  tool: number
  pendingTool: number
  line: number
  nextLine: number
  motion: Motion | null
  dwell: number
  programEnd: boolean
  stopAfterBlock: boolean
  homing: boolean
  workZero: boolean
  motionModal: 0 | 1
  programFeed: number
  spindleSet: number
  spindleOn: boolean
  spindleDir: 1 | -1
  toolClamped: boolean
  aBrake: boolean
  coolant: boolean
  mist: boolean
  lube: number
  workLight: boolean
  chipConveyor: boolean
  mdi: { buffer: string; history: string[]; running: boolean }
  feedOverride: number
  spindleOverride: number
  rapidOverride: number
  singleBlock: boolean
  optStop: boolean
  dryRun: boolean
  blockSkip: boolean
  machineLock: boolean
  jogAxis: Axis
  jogStep: number
  jogRapid: boolean
  jogFeed: number
  cursor: Record<Screen, number>
  alarms: Alarm[]
  alarmSeq: number
  cycleTime: number
  lastCycle: number
  partCount: number
  message: Message | null
  help: boolean
  service: ServiceState
  probe: ProbeState
  sim: SimState
  test: TestState
  toolMeasure: ToolMeasureState
  search: SearchState
  /**
   * Parameter values by catalogue id. A key may be absent (an older machine INI, a section added
   * later): read through paramValue (service-data.ts), not by index.
   */
  params: Record<string, number>
}
