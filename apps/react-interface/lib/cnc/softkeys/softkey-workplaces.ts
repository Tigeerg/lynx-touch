// The workplace sets: the bar depends on the chosen workplace (НАЛАДКА, ПРОГОН, АВТО) before it
// depends on the screen. The screens with their own wizard (offsets, service, editor) keep their
// own sets from softkey-screens.ts.
import {
  ArrowRightToLine,
  Calculator,
  CircleHelp,
  CirclePause,
  Crosshair,
  Disc3,
  Eye,
  Flame,
  FlaskConical,
  FolderOpen,
  House,
  LocateFixed,
  Package,
  Play,
  Ruler,
  Save,
  ScanSearch,
  Search,
  SquarePen,
  StepForward,
  Wrench,
  type LucideIcon,
} from 'lucide-react'
import { AUTO_VIEW_LABEL, SOFTKEY_LABEL } from '../data'
import { PROBE_TOOL } from '../probe'
import { TEST_RUN_MODE_LABEL } from '../test'
import { AUTO_VIEWS, type MachineState, type TestRunMode } from '../types'
import { slots, type Softkey } from './softkey-slots'

/** The F4 key shows what ПУСК will do: a flask, a play, a step, or the handwheel. */
const TEST_RUN_MODE_ICON: Record<TestRunMode, LucideIcon> = {
  sim: FlaskConical,
  dry: Play,
  step: StepForward,
  mpg: Disc3,
}

// The SETUP workplace: the F1-F8 set for manual preparation (tool measuring, base setting,
// parking) on the start Position screen.
export const setupPos = (s: MachineState, busy: boolean): (Softkey | null)[] =>
  slots([
    {
      label: SOFTKEY_LABEL.TOOL_PROBE,
      icon: Ruler,
      // Opens the measuring window with the schematics; the run itself is started inside it
      // (`TOOL_MEASURE_START`), so F1 no longer writes a length behind the operator's back.
      action: { type: 'TOOL_MEASURE' },
      disabled: busy || s.spindleOn || s.tool === PROBE_TOOL,
    },
    {
      label: SOFTKEY_LABEL.PROBE_OPEN,
      icon: LocateFixed,
      action: { type: 'PROBE_OPEN', open: true },
      disabled: busy,
    },
    { label: 'Запись базы', icon: Save, action: { type: 'OFFSET_TEACH', axis: s.jogAxis }, disabled: busy },
    // The base table lives on the "Смещения" tab; the slot stays as a reserve so F1-F8 keep
    // their positions until the operator picks what goes here.
    {
      label: 'Резерв',
      icon: CircleHelp,
      action: { type: 'HINT', text: 'Свободный слот: «Смещения» есть на вкладке' },
      disabled: true,
      reason: 'Свободный слот: «Смещения» есть на вкладке',
    },
    {
      label: SOFTKEY_LABEL.GO_SAFE,
      icon: House,
      action: { type: 'GO_SAFE' },
      disabled: s.run === 'ESTOP' || s.run === 'ALARM' || busy || s.machineLock,
    },
    {
      label: 'В ноль детали',
      icon: Crosshair,
      action: { type: 'GO_WORK_ZERO' },
      disabled: s.run === 'ESTOP' || s.run === 'ALARM' || busy || s.machineLock,
    },
    { label: 'Прогрев', icon: Flame, action: { type: 'SPINDLE_WARMUP' }, disabled: busy || s.spindleOn },
    { label: 'Калькулятор', icon: Calculator, action: { type: 'CALC', open: true }, active: s.calc },
  ])

// The PROGON workplace: checking the NC program without a real cut. F4 chooses what ПУСК
// starts (simulation, dry run, step run or handwheel run), F5/F6 are the block modes, F7 the NC bounding box
// and F8 the AUTO view cycle (same words as AUTO, so there is one "Отображение" and no duplicate key).
// The start itself is the console's ПУСК, not a softkey: one way to start a run on the panel.
export const testWorkplace = (s: MachineState, busy: boolean): (Softkey | null)[] =>
  slots([
    { label: 'Каталог УП', icon: FolderOpen, action: { type: 'FILE_DIALOG', open: true }, disabled: busy },
    { label: 'Редактор', icon: SquarePen, action: { type: 'EDIT_MODE', on: true }, disabled: busy },
    { label: 'Поиск / Замена', icon: Search, action: { type: 'SEARCH', open: true } },
    {
      label: `Режим: ${TEST_RUN_MODE_LABEL[s.test.runMode]}`,
      icon: TEST_RUN_MODE_ICON[s.test.runMode],
      action: { type: 'TEST_RUN_MODE' },
      active: true,
      // The mode decides what ПУСК will start, so it is chosen before the run, never inside it.
      disabled: busy || s.sim.active,
      reason: 'Остановите прогон, чтобы сменить режим',
    },
    { label: 'Покадр.', icon: StepForward, action: { type: 'TOGGLE', key: 'singleBlock' }, active: s.singleBlock },
    { label: 'M01', icon: CirclePause, action: { type: 'TOGGLE', key: 'optStop' }, active: s.optStop },
    { label: 'Габариты УП', icon: Ruler, action: { type: 'TEST_BBOX' }, active: s.test.bbox },
    {
      label: `Отображение: ${AUTO_VIEW_LABEL[s.test.autoView]}`,
      icon: Eye,
      action: { type: 'AUTO_VIEW_CYCLE' },
    },
  ])

// The AUTO workplace: the F1-F8 set for real machining on the program screen.
// The spare slot (F7) is left empty for a user macro.
export const runProg = (s: MachineState, busy: boolean): (Softkey | null)[] =>
  slots([
    { label: 'Каталог УП', icon: FolderOpen, action: { type: 'FILE_DIALOG', open: true }, disabled: busy },
    { label: 'Старт с кадра', icon: ArrowRightToLine, action: { type: 'PROG_TO_CURSOR' }, disabled: busy },
    { label: SOFTKEY_LABEL.TOOL_LIFE, icon: Wrench, action: { type: 'TOOL_LIFE' } },
    {
      label: 'Резерв',
      icon: CircleHelp,
      action: { type: 'HINT', text: 'Свободный слот под макрос' },
      disabled: true,
      reason: 'Свободный слот под макрос',
    },
    { label: SOFTKEY_LABEL.PART_COUNTER_RESET, icon: Package, action: { type: 'PART_COUNTER_RESET' } },
    { label: 'Проверка слома', icon: ScanSearch, action: { type: 'BROKEN_TOOL_CHECK' } },
    {
      label: 'Резерв',
      icon: CircleHelp,
      action: { type: 'HINT', text: 'Свободный слот под макрос' },
      disabled: true,
      reason: 'Свободный слот под макрос',
    },
    {
      label: `Отображение: ${AUTO_VIEW_LABEL[AUTO_VIEWS[(AUTO_VIEWS.indexOf(s.test.autoView) + 1) % AUTO_VIEWS.length]]}`,
      icon: Eye,
      action: { type: 'AUTO_VIEW_CYCLE' },
    },
  ])
