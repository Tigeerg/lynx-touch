import type { Alarm, AutoView, Axis, Mode, ProgramFile, RunState, Screen, Tool, WorkOffset, Workplace } from './types'

export const MACHINE_NAME = 'VMC-850'
export const PROGRAM_NAME = 'O1024'
export const PROGRAM_TITLE = 'КОРПУС ФЛАНЦА'
export const RAPID_RATE = 15000
export const DRY_RUN_RATE = 6000
export const MAX_RPM = 12000
/** Milliseconds in a minute: converts an mm/min feed into the per-millisecond travel. */
export const MS_PER_MINUTE = 60_000
export const JOG_STEPS = [0.001, 0.01, 0.1]
export const RAPID_JOG_MULT = 10
export const RAPID_STEPS = [5, 25, 50, 100]

export const SOFT_LIMITS: Record<Axis, [number, number]> = {
  X: [-400, 400],
  Y: [-250, 250],
  Z: [-350, 0],
  A: [-99999, 99999],
}

export const PROGRAM: string[] = [
  `${PROGRAM_NAME} (${PROGRAM_TITLE})`,
  'N10 G21 G90 G17 G40 G49 G80',
  'N20 T1 M06 (ФРЕЗА D12)',
  'N30 G54 G00 X0 Y0',
  'N40 S8000 M03',
  'N50 G43 H01 Z50.0 M08',
  'N60 G00 X-40.0 Y-30.0',
  'N70 Z5.0',
  'N80 G01 Z-2.0 F400',
  'N90 G01 X40.0 F1200',
  'N100 Y30.0',
  'N110 X-40.0',
  'N120 Y-30.0',
  'N130 G00 Z5.0',
  'N135 M01 (КОНТРОЛЬ РАЗМЕРА)',
  'N140 X-30.0 Y-20.0',
  'N150 G01 Z-4.0 F400',
  'N160 X30.0 F1000',
  'N170 Y20.0',
  'N180 X-30.0',
  'N190 Y-20.0',
  'N200 G00 Z50.0 M09',
  'N210 M05',
  'N220 G91 G28 Z0',
  'N230 M30',
]

export const PROGRAM_FILES: ProgramFile[] = [
  { name: PROGRAM_NAME, title: PROGRAM_TITLE, lines: PROGRAM, modified: '08.02.2026 14:20' },
  {
    name: 'O2048',
    title: 'ПЛИТА ОСНОВАНИЯ',
    modified: '05.02.2026 09:12',
    lines: [
      'O2048 (ПЛИТА ОСНОВАНИЯ)',
      'N10 G21 G90 G17 G40 G49 G80',
      'N20 T2 M06 (СВЕРЛО D6.8)',
      'N30 G54 G00 X0 Y0',
      'N40 S2400 M03',
      'N50 G43 H02 Z50.0 M08',
      'N60 G00 X-50.0 Y-30.0 Z3.0',
      'N70 G01 Z-15.0 F180',
      'N80 G00 Z3.0',
      'N90 X50.0',
      'N100 G01 Z-15.0 F180',
      'N110 G00 Z3.0',
      'N120 Y30.0',
      'N130 G01 Z-15.0 F180',
      'N140 G00 Z3.0',
      'N150 X-50.0',
      'N160 G01 Z-15.0 F180',
      'N170 G00 Z50.0 M09',
      'N180 M05',
      'N190 G91 G28 Z0',
      'N200 M30',
    ],
  },
  {
    name: 'O3100',
    title: 'КРЫШКА ПОД ПОДШИПНИК',
    modified: '29.01.2026 16:47',
    lines: [
      'O3100 (КРЫШКА ПОД ПОДШИПНИК)',
      'N10 G21 G90 G17 G40 G49 G80',
      'N20 T4 M06 (ФРЕЗА ТОРЦЕВАЯ D50)',
      'N30 G55 G00 X-70.0 Y0',
      'N40 S3500 M03',
      'N50 G43 H04 Z50.0 M08',
      'N60 Z2.0',
      'N70 G01 Z-0.5 F300',
      'N80 X70.0 F1500',
      'N90 G00 Z50.0 M09',
      'N100 M05',
      'N110 G91 G28 Z0',
      'N120 M30',
    ],
  },
  {
    name: 'O0007',
    title: 'ПРОГРЕВ ШПИНДЕЛЯ',
    modified: '12.01.2026 07:05',
    lines: [
      'O0007 (ПРОГРЕВ ШПИНДЕЛЯ)',
      'N10 G21 G90 G17',
      'N20 S2000 M03',
      'N30 G04 P5',
      'N40 S5000',
      'N50 G04 P5',
      'N60 S8000',
      'N70 G04 P5',
      'N80 M05',
      'N90 M30',
    ],
  },
  // The real CAM program from `data/cad-testdata/Test_model.ngc` (110 586 blocks, 2.9 MB): too big
  // for the bundle, so it is fetched the first time the operator opens it.
  {
    name: 'TEST_MODEL',
    title: 'ТЕСТОВАЯ МОДЕЛЬ (PLOT CNC EMC)',
    modified: '28.10.2026 12:00',
    lines: [],
    source: '/programs/Test_model.ngc',
  },
]

export const INITIAL_OFFSETS: WorkOffset[] = [
  { code: 'G54', values: { X: -120.5, Y: 35.25, Z: -210.4, A: 0 } },
  { code: 'G55', values: { X: -320.0, Y: 35.25, Z: -210.4, A: 90 } },
  { code: 'G56', values: { X: 0, Y: 0, Z: 0, A: 0 } },
  { code: 'G57', values: { X: 0, Y: 0, Z: 0, A: 0 } },
  { code: 'G58', values: { X: 0, Y: 0, Z: 0, A: 0 } },
  { code: 'G59', values: { X: 0, Y: 0, Z: 0, A: 0 } },
]

export const INITIAL_TOOLS: Tool[] = [
  { number: 1, name: 'Фреза концевая 4z', diameter: 12, length: 85.214, life: 72 },
  { number: 2, name: 'Сверло спиральное', diameter: 6.8, length: 112.06, life: 54 },
  { number: 3, name: 'Метчик M8', diameter: 8, length: 96.3, life: 88 },
  { number: 4, name: 'Фреза торцевая', diameter: 50, length: 62.718, life: 8 },
  { number: 5, name: 'Зенковка 90°', diameter: 16, length: 78.45, life: 65 },
  { number: 6, name: 'Фреза сферическая', diameter: 6, length: 90.11, life: 41 },
  { number: 7, name: 'Сверло центровочное', diameter: 10, length: 70.902, life: 93 },
  { number: 8, name: 'Головка расточная', diameter: 32, length: 128.5, life: 77 },
  { number: 99, name: '3D щуп Renishaw OMP40-2', diameter: 2, length: 98.45, life: 100 },
]

export const INITIAL_ALARMS: Alarm[] = [
  { id: 'a3', code: 2051, level: 'warning', text: 'Ресурс инструмента T04 менее 10%', time: '07:42:10', active: true },
  { id: 'a2', code: 3012, level: 'info', text: 'Плановое ТО шпинделя через 48 ч', time: '07:30:02', active: true },
  { id: 'a1', code: 1104, level: 'error', text: 'Низкий уровень СОЖ в баке', time: '06:58:44', active: false },
]

export const MODE_LABEL: Record<Mode, string> = {
  AUTO: 'АВТО',
  MDI: 'MDI',
  JOG: 'РУЧНОЙ',
  MPG: 'МАХОВИК',
}

export const WORKPLACE_LABEL: Record<Workplace, string> = {
  SETUP: 'НАЛАДКА',
  TEST: 'ПРОГОН',
  RUN: 'АВТО',
}

export const AUTO_VIEW_LABEL: Record<AutoView, string> = {
  trajectory: 'траектория',
  gcode: 'gcode',
  both: 'совместно',
  overlay: 'УП поверх',
}

// The header of the AUTO and PROGON viewer panel, per view: the softkey names the switch
// (AUTO_VIEW_LABEL), the header names what is on screen (the viewer, the code, or both).
export const AUTO_VIEW_TITLE: Record<AutoView, string> = {
  trajectory: 'Траектория',
  gcode: 'Программа',
  both: 'Программа и траектория',
  overlay: 'УП поверх траектории',
}

export const RUN_LABEL: Record<RunState, string> = {
  IDLE: 'ГОТОВ',
  RUN: 'ВЫПОЛНЕНИЕ',
  HOLD: 'СТОП ПОДАЧИ',
  ALARM: 'АВАРИЯ',
  ESTOP: 'АВАРИЙНЫЙ СТОП',
}

export const SCREEN_LABEL: Record<Screen, string> = {
  pos: 'Позиция',
  prog: 'Программа',
  offset: 'Смещения',
  tool: 'Инструмент',
  alarm: 'Сообщения',
  service: 'Сервис',
}

// The softkey label of an operation, named once: the same action was labelled two ways inside
// softkeys.ts (tool measuring, safe retract, probe open), so the Qt port had to guess which one to
// copy (CLAUDE.md, section 5: a word needed in two places has one source, not a copy).
export const SOFTKEY_LABEL = {
  TOOL_PROBE: 'Обмер инструмента',
  PROBE_OPEN: '3D щуп',
  GO_SAFE: 'Безопасный отвод',
  TOOL_LIFE: 'Ресурс инструмента',
  PART_COUNTER_RESET: 'Счётчики деталей',
} as const
