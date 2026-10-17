import type { PathView } from './toolpath'

export type Axis = 'X' | 'Y' | 'Z' | 'A'
export const AXES: Axis[] = ['X', 'Y', 'Z', 'A']
export type Axes = Record<Axis, number>

export type Mode = 'AUTO' | 'MDI' | 'JOG' | 'MPG'
export const MODES: Mode[] = ['AUTO', 'MDI', 'JOG', 'MPG']

// Рабочее место оператора: верхний уровень панели. Определяет набор софт-кеев и
// компоновку экрана, независимо от режима станка (Mode) и экрана (Screen).
export type Workplace = 'SETUP' | 'TEST' | 'RUN'
export const WORKPLACES: Workplace[] = ['SETUP', 'TEST', 'RUN']

// Вкладки экрана: «Работа» объединяет прежние «Позиция» и «Программа»; её
// содержимое выбирает рабочее место (Наладка/Прогон/Авто).
export type Tab = 'work' | 'offset' | 'tool' | 'alarm' | 'service'
export const TABS: Tab[] = ['work', 'offset', 'tool', 'alarm', 'service']
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
export const SCREENS: Screen[] = ['pos', 'prog', 'offset', 'tool', 'alarm', 'service']

export const MODE_START_SCREEN: Record<Mode, Screen> = { AUTO: 'prog', MDI: 'pos', JOG: 'pos', MPG: 'pos' }
export const MODE_LOCKED_SCREENS: Record<Mode, Screen[]> = {
  AUTO: ['offset', 'tool', 'service'],
  MDI: ['prog', 'service'],
  JOG: [],
  MPG: [],
}

export const MPG_STEPS = [0.001, 0.01, 0.1]
export const mpgMultiplier = (step: number) => Math.round(step / 0.001)
export const JOG_FEEDS = [100, 500, 1000, 3000]
export const MDI_HISTORY_SIZE = 10

export type ServiceSection = 'system' | 'linuxcnc' | 'motion' | 'joints' | 'pid' | 'mesa' | 'hal' | 'ladder' | 'help'
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

// Рабочее место ПРОГОН: показ графики и шторки кода, габариты УП, ракурс.
export interface TestState {
  /** Шторка с G-кодом скрыта (F8 «Скрыть код»). */
  curtain: boolean
  /** Показаны габариты УП (F5). */
  bbox: boolean
  /** Графика очищена: траектория не рисуется (F6). */
  hidePath: boolean
  /** Работа: показывать код, а не графику (F8 «Вид»). */
  autoCode: boolean
  view: PathView
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
  /** Если задано — элемент является блоком сравнения, а не контактом */
  cmp?: LadderCompare
}

export type CoilKind = 'coil' | 'set' | 'reset' | 'ton' | 'tof' | 'ctu' | 'ctd'

export interface LadderCoil extends LadderContact {
  kind?: CoilKind
  /** Уставка таймера (мс) или счётчика (импульсы) */
  preset?: number
}

export interface LadderRung {
  title: string
  contacts: LadderContact[]
  coil: LadderCoil
  /** Параллельные ветви (ИЛИ): каждая ветвь — последовательность контактов (И) */
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
  /** Индекс ветви: 0 — основная линия, 1.. — параллельные (ИЛИ) ветви */
  path: number
  manager: boolean
  pick: number
  form: LadderForm | null
  mem: LadderMem
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
  search: SearchState
  params: Record<string, number>
}
