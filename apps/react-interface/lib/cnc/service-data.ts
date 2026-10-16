import {
  coilKind,
  DEFAULT_PRESET_COUNT,
  DEFAULT_PRESET_MS,
  isCounterKind,
  isLatchKind,
  isTimerKind,
  rungBranches,
  savedRungs,
} from './ladder'
import {
  AXES,
  type Axes,
  type Axis,
  type LadderContact,
  type LadderMem,
  type LadderRung,
  type MachineState,
  type ServiceSection,
} from './types'

export const INI_FILE = 'vmc850.ini'

export const SERVICE_LABEL: Record<ServiceSection, string> = {
  system: 'Система',
  linuxcnc: 'LinuxCNC',
  motion: 'Траектория',
  joints: 'Оси',
  pid: 'PID',
  mesa: 'Mesa',
  hal: 'HAL',
  ladder: 'Ladder',
  help: 'Справка',
}

export const SERVICE_HINT: Record<ServiceSection, string> = {
  system: 'Интерфейс и [DISPLAY]',
  linuxcnc: '[KINS] [RS274NGC] [HAL] [EMCIO]',
  mesa: 'hostmot2 · плата, stepgen, PWM',
  motion: '[TRAJ] [SPINDLE] [EMCMOT]',
  joints: '[JOINT_n] пределы, поиск нуля',
  pid: 'Контуры регулирования',
  hal: 'Монитор пинов HAL',
  ladder: 'ClassicLadder, онлайн',
  help: 'Раскладка пульта и клавиши',
}

export interface ParamDef {
  id: string
  group: string
  key: string
  label: string
  unit?: string
  step: number
  min: number
  max: number
  digits: number
  default: number
  options?: string[]
  free?: boolean
  restart?: boolean
}

interface JointParamDef extends Omit<ParamDef, 'id' | 'group' | 'default'> {
  defaults: Axes
}

const p = (def: Omit<ParamDef, 'id' | 'digits'> & { digits?: number }): ParamDef => ({
  digits: 0,
  ...def,
  id: `${def.group}.${def.key}`,
})

export const THEME_PARAM = 'DISPLAY.THEME'
export const UI_MODE_PARAM = 'DISPLAY.UI_MODE'
export const TOOLPATH_PARAM = 'DISPLAY.TOOLPATH_PREVIEW'
export const SCREEN_SIZE_PARAM = 'DISPLAY.SCREEN_SIZE'
export const MAX_FEED_PARAM = 'DISPLAY.MAX_FEED_OVERRIDE'
export const MAX_SPINDLE_PARAM = 'DISPLAY.MAX_SPINDLE_OVERRIDE'

export const SYSTEM_PARAMS: ParamDef[] = [
  p({
    group: 'DISPLAY',
    key: 'THEME',
    label: 'Тема интерфейса',
    step: 1,
    min: 0,
    max: 3,
    default: 0,
    options: ['Светлая', 'Тёмная', 'Графит', 'Мята'],
    free: true,
  }),
  p({
    group: 'DISPLAY',
    key: 'UI_MODE',
    label: 'Режим ЧПУ',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['Пульт', 'Сенсор'],
    free: true,
  }),
  p({
    group: 'DISPLAY',
    key: 'TOOLPATH_PREVIEW',
    label: 'Предпросмотр траектории',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['Скрыт', 'Показан'],
    free: true,
  }),
  p({
    group: 'DISPLAY',
    key: 'SCREEN_SIZE',
    label: 'Размер экрана',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['7 дюймов', '10 дюймов'],
    free: true,
  }),
  p({
    group: 'DISPLAY',
    key: 'LANGUAGE',
    label: 'Язык интерфейса',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['Русский', 'English'],
    free: true,
  }),
  p({
    group: 'EMC',
    key: 'MACHINE',
    label: 'Имя станка',
    step: 1,
    min: 0,
    max: 2,
    default: 0,
    options: ['VMC850', 'VMC850M', 'Gantry'],
    restart: true,
  }),
  p({ group: 'EMC', key: 'DEBUG', label: 'Уровень отладки', step: 1, min: 0, max: 7, default: 0 }),
  p({
    group: 'DISPLAY',
    key: 'DISPLAY',
    label: 'Модуль интерфейса',
    step: 1,
    min: 0,
    max: 2,
    default: 0,
    options: ['axis', 'qtvcp', 'gmoccapy'],
    restart: true,
  }),
  p({
    group: 'DISPLAY',
    key: 'POSITION_OFFSET',
    label: 'Начало отсчёта позиции',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['Относительно детали', 'Относительно машины'],
  }),
  p({
    group: 'DISPLAY',
    key: 'POSITION_FEEDBACK',
    label: 'Отображение позиции',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['Фактическая', 'Заданная'],
  }),
  p({
    group: 'DISPLAY',
    key: 'CYCLE_TIME',
    label: 'Период обновления экрана',
    unit: 'мс',
    step: 50,
    min: 50,
    max: 500,
    default: 100,
  }),
  p({ group: 'DISPLAY', key: 'ARCDIVISION', label: 'Сегментов на дугу', step: 8, min: 8, max: 128, default: 64 }),
  p({
    group: 'DISPLAY',
    key: 'MAX_FEED_OVERRIDE',
    label: 'Макс. коррекция подачи',
    unit: '%',
    step: 10,
    min: 100,
    max: 200,
    default: 150,
  }),
  p({
    group: 'DISPLAY',
    key: 'MIN_SPINDLE_OVERRIDE',
    label: 'Мин. коррекция шпинделя',
    unit: '%',
    step: 5,
    min: 0,
    max: 100,
    default: 0,
  }),
  p({
    group: 'DISPLAY',
    key: 'MAX_SPINDLE_OVERRIDE',
    label: 'Макс. коррекция шпинделя',
    unit: '%',
    step: 10,
    min: 100,
    max: 200,
    default: 120,
  }),
  p({
    group: 'DISPLAY',
    key: 'DEFAULT_SPINDLE_SPEED',
    label: 'Обороты шпинделя по умолч.',
    unit: 'об/мин',
    step: 100,
    min: 0,
    max: 24000,
    default: 3000,
  }),
  p({
    group: 'DISPLAY',
    key: 'SPINDLE_INCREMENT',
    label: 'Шаг оборотов',
    unit: 'об/мин',
    step: 50,
    min: 10,
    max: 1000,
    default: 100,
  }),
  p({
    group: 'DISPLAY',
    key: 'INCREMENTS',
    label: 'Ручные шаги (JOG)',
    step: 1,
    min: 0,
    max: 2,
    default: 1,
    options: ['Непрерывно', '1·0.1·0.01·0.001', '1·0.1·0.01·0.001·0.0001'],
  }),
  p({
    group: 'DISPLAY',
    key: 'MIN_LINEAR_VELOCITY',
    label: 'Мин. линейная скорость',
    unit: 'мм/с',
    step: 5,
    min: 0,
    max: 50,
    default: 0,
  }),
  p({
    group: 'DISPLAY',
    key: 'MIN_ANGULAR_VELOCITY',
    label: 'Мин. угловая скорость',
    unit: '°/с',
    step: 0.5,
    min: 0,
    max: 30,
    digits: 1,
    default: 0,
  }),
  p({
    group: 'DISPLAY',
    key: 'DEFAULT_ANGULAR_VELOCITY',
    label: 'Угловая скорость по умолч.',
    unit: '°/с',
    step: 1,
    min: 1,
    max: 180,
    default: 30,
  }),
  p({
    group: 'DISPLAY',
    key: 'MAX_ANGULAR_VELOCITY',
    label: 'Макс. угловая скорость',
    unit: '°/с',
    step: 5,
    min: 10,
    max: 720,
    default: 90,
  }),
  p({
    group: 'DISPLAY',
    key: 'PROGRAM_PREFIX',
    label: 'Каталог программ',
    step: 1,
    min: 0,
    max: 2,
    default: 1,
    options: ['—', '~/linuxcnc/nc_files', '/home/cnc/nc_files'],
  }),
  p({
    group: 'DISPLAY',
    key: 'EDITOR',
    label: 'Внешний редактор',
    step: 1,
    min: 0,
    max: 2,
    default: 0,
    options: ['—', 'gedit', 'nano'],
  }),
  p({
    group: 'DISPLAY',
    key: 'GEOMETRY',
    label: 'Геометрия окна',
    step: 1,
    min: 0,
    max: 2,
    default: 1,
    options: ['—', 'x400 y400', '1024x600'],
  }),
  p({
    group: 'DISPLAY',
    key: 'INTRO_GRAPHIC',
    label: 'Заставка',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['—', 'linuxcnc.gif'],
  }),
  p({ group: 'DISPLAY', key: 'INTRO_TIME', label: 'Время заставки', unit: 'с', step: 1, min: 0, max: 30, default: 5 }),
  p({
    group: 'FILTER',
    key: 'PROGRAM_EXTENSION',
    label: 'Расширения программ',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['.ngc .nc', '.ngc .nc .tap .py'],
  }),
  p({
    group: 'TRAJ',
    key: 'LINEAR_UNITS',
    label: 'Линейные единицы',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['мм', 'дюйм'],
    restart: true,
  }),
]

export const MOTION_PARAMS: ParamDef[] = [
  p({
    group: 'EMCMOT',
    key: 'SERVO_PERIOD',
    label: 'Период servo-потока',
    unit: 'нс',
    step: 250000,
    min: 250000,
    max: 2000000,
    default: 1000000,
    restart: true,
  }),
  p({
    group: 'TASK',
    key: 'CYCLE_TIME',
    label: 'Период task-цикла',
    unit: 'с',
    step: 0.001,
    min: 0.001,
    max: 0.05,
    default: 0.01,
    digits: 3,
    restart: true,
  }),
  p({
    group: 'TRAJ',
    key: 'COORDINATES',
    label: 'Состав координат',
    step: 1,
    min: 0,
    max: 2,
    default: 0,
    options: ['X Y Z A', 'X Y Z', 'X Y Z A B C'],
    restart: true,
  }),
  p({
    group: 'TRAJ',
    key: 'ANGULAR_UNITS',
    label: 'Угловые единицы',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['градус', 'рад'],
    restart: true,
  }),
  p({
    group: 'TRAJ',
    key: 'MAX_LINEAR_VELOCITY',
    label: 'Макс. линейная скорость',
    unit: 'мм/с',
    step: 5,
    min: 10,
    max: 500,
    default: 250,
  }),
  p({
    group: 'TRAJ',
    key: 'DEFAULT_LINEAR_VELOCITY',
    label: 'Скорость ручн. по умолч.',
    unit: 'мм/с',
    step: 5,
    min: 5,
    max: 250,
    default: 50,
  }),
  p({
    group: 'TRAJ',
    key: 'MAX_LINEAR_ACCELERATION',
    label: 'Макс. ускорение',
    unit: 'мм/с²',
    step: 50,
    min: 100,
    max: 5000,
    default: 1500,
  }),
  p({
    group: 'TRAJ',
    key: 'POSITION_FILE',
    label: 'Файл позиции',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['linuxcnc.pos', '—'],
  }),
  p({
    group: 'TRAJ',
    key: 'ARC_BLEND_ENABLE',
    label: 'Сглаживание траектории',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['Выкл', 'Вкл'],
  }),
  p({
    group: 'TRAJ',
    key: 'ARC_BLEND_GAP_CYCLES',
    label: 'Зазор сглаживания',
    unit: 'цикл',
    step: 1,
    min: 1,
    max: 10,
    default: 4,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'MAX_FORWARD_VELOCITY',
    label: 'Макс. обороты шпинделя',
    unit: 'об/мин',
    step: 500,
    min: 1000,
    max: 24000,
    default: 12000,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'MIN_FORWARD_VELOCITY',
    label: 'Мин. обороты шпинделя',
    unit: 'об/мин',
    step: 50,
    min: 0,
    max: 2000,
    default: 300,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'INCREMENT',
    label: 'Шаг оборотов (ручн.)',
    unit: 'об/мин',
    step: 50,
    min: 50,
    max: 1000,
    default: 100,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'OUTPUT_SCALE',
    label: 'Масштаб выхода',
    unit: 'об/мин',
    step: 500,
    min: 100,
    max: 24000,
    default: 12000,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'OUTPUT_MIN_LIMIT',
    label: 'Мин. выход',
    unit: 'об/мин',
    step: 100,
    min: 0,
    max: 24000,
    default: 0,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'OUTPUT_MAX_LIMIT',
    label: 'Макс. выход',
    unit: 'об/мин',
    step: 500,
    min: 100,
    max: 24000,
    default: 12000,
  }),
  p({ group: 'SPINDLE_0', key: 'P', label: 'Шпиндель: пропорц.', step: 0.1, min: 0, max: 100, digits: 1, default: 0 }),
  p({ group: 'SPINDLE_0', key: 'I', label: 'Шпиндель: интеграл.', step: 0.1, min: 0, max: 100, digits: 1, default: 0 }),
  p({
    group: 'SPINDLE_0',
    key: 'D',
    label: 'Шпиндель: дифференц.',
    step: 0.1,
    min: 0,
    max: 100,
    digits: 1,
    default: 0,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'FF0',
    label: 'Шпиндель: упрежд. 0',
    step: 0.1,
    min: 0,
    max: 10,
    digits: 1,
    default: 1,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'FF1',
    label: 'Шпиндель: упрежд. 1',
    step: 0.01,
    min: 0,
    max: 10,
    digits: 2,
    default: 0,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'FF2',
    label: 'Шпиндель: упрежд. 2',
    step: 0.01,
    min: 0,
    max: 10,
    digits: 2,
    default: 0,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'BIAS',
    label: 'Шпиндель: смещение',
    step: 0.1,
    min: -1000,
    max: 1000,
    digits: 1,
    default: 0,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'DEADBAND',
    label: 'Шпиндель: мёртвая зона',
    unit: 'об/мин',
    step: 1,
    min: 0,
    max: 100,
    default: 5,
  }),
  p({
    group: 'SPINDLE_0',
    key: 'MAX_OUTPUT',
    label: 'Шпиндель: огр. выхода',
    unit: 'об/мин',
    step: 500,
    min: 0,
    max: 24000,
    default: 12000,
  }),
  p({
    group: 'EMCIO',
    key: 'TOOL_CHANGE_QUILL_UP',
    label: 'Подъём Z при смене инстр.',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['Нет', 'Да'],
  }),
  p({
    group: 'EMCIO',
    key: 'TOOL_CHANGE_WITH_SPINDLE_ON',
    label: 'Смена при вращении',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['Нет', 'Да'],
  }),
]

const OFF_ON = ['Нет', 'Да']

export const LINUXCNC_PARAMS: ParamDef[] = [
  p({
    group: 'KINS',
    key: 'KINEMATICS',
    label: 'Модуль кинематики',
    step: 1,
    min: 0,
    max: 2,
    default: 0,
    options: ['trivkins XYZA', 'trivkins XYZ', 'gantrykins'],
    restart: true,
  }),
  p({ group: 'KINS', key: 'JOINTS', label: 'Число сочленений', step: 1, min: 3, max: 9, default: 4, restart: true }),
  p({
    group: 'RS274NGC',
    key: 'RS274NGC_STARTUP_CODE',
    label: 'Стартовые G-коды',
    step: 1,
    min: 0,
    max: 2,
    default: 0,
    options: ['G17 G21 G90 G54', 'G17 G21 G90', 'G18 G21 G90'],
  }),
  p({
    group: 'RS274NGC',
    key: 'PARAMETER_FILE',
    label: 'Файл параметров',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['linuxcnc.var', 'vmc850.var'],
    restart: true,
  }),
  p({ group: 'RS274NGC', key: 'FEATURES', label: 'Флаги интерпретатора', step: 1, min: 0, max: 63, default: 12 }),
  p({
    group: 'RS274NGC',
    key: 'ON_ABORT_COMMAND',
    label: 'Подпрограмма при аборте',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['—', 'o<on_abort>'],
  }),
  p({
    group: 'EMCIO',
    key: 'CYCLE_TIME',
    label: 'Период EMCIO',
    unit: 'с',
    step: 0.001,
    min: 0.001,
    max: 0.05,
    default: 0.01,
    digits: 3,
  }),
  p({
    group: 'EMCIO',
    key: 'TOOL_TABLE',
    label: 'Таблица инструмента',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['tool.tbl', 'tool_atc.tbl'],
  }),
  p({
    group: 'EMCIO',
    key: 'RANDOM_TOOLCHANGER',
    label: 'Случайный магазин',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: OFF_ON,
    restart: true,
  }),
  p({
    group: 'HAL',
    key: 'HALFILE',
    label: 'Основной HAL-файл',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['vmc850.hal', 'vmc850_sim.hal'],
    restart: true,
  }),
  p({
    group: 'HAL',
    key: 'HALUI',
    label: 'Модуль halui',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['Выкл', 'halui'],
    restart: true,
  }),
  p({
    group: 'HAL',
    key: 'POSTGUI_HALFILE',
    label: 'Post-GUI HAL-файл',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['—', 'custom_postgui.hal'],
  }),
  p({
    group: 'HAL',
    key: 'SHUTDOWN',
    label: 'HAL при выходе',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['—', 'shutdown.hal'],
  }),
  p({
    group: 'EMCMOT',
    key: 'COMM_TIMEOUT',
    label: 'Тайм-аут связи motion',
    unit: 'с',
    step: 0.5,
    min: 0.5,
    max: 5,
    default: 1,
    digits: 1,
  }),
  p({
    group: 'EMCMOT',
    key: 'NUM_DIO',
    label: 'Цифровые I/O motion',
    step: 1,
    min: 0,
    max: 64,
    default: 8,
    restart: true,
  }),
  p({
    group: 'TRAJ',
    key: 'NO_FORCE_HOMING',
    label: 'Работа без поиска нуля',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: OFF_ON,
  }),
]

export const MESA_PARAMS: ParamDef[] = [
  p({
    group: 'HOSTMOT2',
    key: 'BOARD',
    label: 'Плата Mesa',
    step: 1,
    min: 0,
    max: 3,
    default: 0,
    options: ['7i96S', '7i76E', '7i92T + 7i76', '7i92 (Ethernet)'],
    restart: true,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'IPADDR',
    label: 'IP-адрес платы',
    step: 1,
    min: 0,
    max: 1,
    default: 0,
    options: ['192.168.1.121', '10.10.10.10'],
    restart: true,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'NUM_STEPGENS',
    label: 'Генераторы шагов',
    step: 1,
    min: 0,
    max: 5,
    default: 4,
    restart: true,
  }),
  p({ group: 'HOSTMOT2', key: 'NUM_ENCODERS', label: 'Энкодеры', step: 1, min: 0, max: 2, default: 1, restart: true }),
  p({
    group: 'HOSTMOT2',
    key: 'NUM_PWMGENS',
    label: 'ШИМ-генераторы',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    restart: true,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'WATCHDOG_TIMEOUT',
    label: 'Тайм-аут watchdog',
    unit: 'мс',
    step: 1,
    min: 1,
    max: 50,
    default: 5,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'DPLL_TIMER',
    label: 'Опережение DPLL',
    unit: 'мкс',
    step: 10,
    min: -500,
    max: 0,
    default: -100,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'STEP_TYPE',
    label: 'Тип выхода stepgen',
    step: 1,
    min: 0,
    max: 2,
    default: 0,
    options: ['Step/Dir', 'Up/Down', 'Quadrature'],
    restart: true,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'CONTROL_TYPE',
    label: 'Режим stepgen',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['Позиция', 'Скорость'],
  }),
  p({
    group: 'HOSTMOT2',
    key: 'STEPLEN',
    label: 'Длительность импульса',
    unit: 'нс',
    step: 500,
    min: 1000,
    max: 20000,
    default: 5000,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'STEPSPACE',
    label: 'Пауза между импульсами',
    unit: 'нс',
    step: 500,
    min: 1000,
    max: 20000,
    default: 5000,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'DIRSETUP',
    label: 'Установка направления',
    unit: 'нс',
    step: 1000,
    min: 1000,
    max: 50000,
    default: 10000,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'DIRHOLD',
    label: 'Удержание направления',
    unit: 'нс',
    step: 500,
    min: 500,
    max: 20000,
    default: 1000,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'PWM_OUTPUT_TYPE',
    label: 'Тип выхода pwmgen',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['PWM', 'PDM'],
    restart: true,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'PWM_FREQUENCY',
    label: 'Частота ШИМ шпинде��я',
    unit: 'Гц',
    step: 1000,
    min: 1000,
    max: 50000,
    default: 20000,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'PDM_FREQUENCY',
    label: 'Частота ЧИМ (PDM)',
    unit: 'Гц',
    step: 100000,
    min: 100000,
    max: 12000000,
    default: 6000000,
  }),
  p({
    group: 'HOSTMOT2',
    key: 'ENCODER_FILTER',
    label: 'Фильтр энкодера',
    step: 1,
    min: 0,
    max: 1,
    default: 1,
    options: ['Выкл', 'Вкл'],
  }),
]

const ax = (X: number, Y: number, Z: number, A: number): Axes => ({ X, Y, Z, A })

export const JOINT_PARAMS: JointParamDef[] = [
  {
    key: 'TYPE',
    label: 'Тип оси',
    step: 1,
    min: 0,
    max: 1,
    digits: 0,
    options: ['LINEAR', 'ANGULAR'],
    defaults: ax(0, 0, 0, 1),
  },
  {
    key: 'MAX_VELOCITY',
    label: 'Макс. скорость',
    unit: 'мм/с',
    step: 5,
    min: 5,
    max: 500,
    digits: 1,
    defaults: ax(250, 250, 200, 360),
  },
  {
    key: 'STEPGEN_MAXVEL',
    label: 'Макс. скорость stepgen',
    unit: 'мм/с',
    step: 5,
    min: 5,
    max: 600,
    digits: 1,
    defaults: ax(300, 300, 240, 400),
  },
  {
    key: 'MAX_ACCELERATION',
    label: 'Макс. ускорение',
    unit: 'мм/с²',
    step: 50,
    min: 50,
    max: 5000,
    digits: 0,
    defaults: ax(1500, 1500, 1200, 2000),
  },
  {
    key: 'STEPGEN_MAXACCEL',
    label: 'Макс. ускорение stepgen',
    unit: 'мм/с²',
    step: 50,
    min: 50,
    max: 8000,
    digits: 0,
    defaults: ax(1800, 1800, 1500, 2200),
  },
  {
    key: 'MIN_LIMIT',
    label: 'Мин. предел',
    unit: 'мм',
    step: 1,
    min: -2000,
    max: 0,
    digits: 3,
    defaults: ax(-850, -500, -550, -99999),
  },
  {
    key: 'MAX_LIMIT',
    label: 'Макс. предел',
    unit: 'мм',
    step: 1,
    min: 0,
    max: 2000,
    digits: 3,
    defaults: ax(0.5, 0.5, 0.5, 99999),
  },
  {
    key: 'FERROR',
    label: 'Ошибка рассогл.',
    unit: 'мм',
    step: 0.01,
    min: 0.01,
    max: 5,
    digits: 3,
    defaults: ax(0.5, 0.5, 0.5, 1),
  },
  {
    key: 'MIN_FERROR',
    label: 'Мин. рассогл.',
    unit: 'мм',
    step: 0.005,
    min: 0.005,
    max: 1,
    digits: 3,
    defaults: ax(0.05, 0.05, 0.05, 0.1),
  },
  {
    key: 'BACKLASH',
    label: 'Люфт',
    unit: 'мм',
    step: 0.001,
    min: 0,
    max: 0.5,
    digits: 3,
    defaults: ax(0.004, 0.006, 0.003, 0.01),
  },
  {
    key: 'SCALE',
    label: 'Масштаб (STEP_SCALE)',
    unit: 'имп/мм',
    step: 10,
    min: 10,
    max: 100000,
    digits: 0,
    defaults: ax(1000, 1000, 1000, 500),
  },
  {
    key: 'HOME',
    label: 'Позиция нуля',
    unit: 'мм',
    step: 0.1,
    min: -100,
    max: 100,
    digits: 3,
    defaults: ax(0, 0, 0, 0),
  },
  {
    key: 'HOME_OFFSET',
    label: 'Смещение нуля',
    unit: 'мм',
    step: 0.1,
    min: -1000,
    max: 1000,
    digits: 3,
    defaults: ax(0, 0, 0, 0),
  },
  {
    key: 'HOME_SEARCH_VEL',
    label: 'Поиск нуля',
    unit: 'мм/с',
    step: 1,
    min: -100,
    max: 100,
    digits: 1,
    defaults: ax(20, 20, 15, 30),
  },
  {
    key: 'HOME_LATCH_VEL',
    label: 'Фиксация нуля',
    unit: 'мм/с',
    step: 0.5,
    min: -20,
    max: 20,
    digits: 1,
    defaults: ax(-1, -1, -1, -2),
  },
  {
    key: 'HOME_FINAL_VEL',
    label: 'Конечная скорость нуля',
    unit: 'мм/с',
    step: 0.5,
    min: 0,
    max: 50,
    digits: 1,
    defaults: ax(5, 5, 2, 5),
  },
  {
    key: 'HOME_USE_INDEX',
    label: 'Метка нуля (index)',
    step: 1,
    min: 0,
    max: 1,
    digits: 0,
    options: OFF_ON,
    defaults: ax(0, 0, 0, 0),
  },
  { key: 'HOME_SEQUENCE', label: 'Порядок поиска', step: 1, min: 0, max: 9, digits: 0, defaults: ax(1, 1, 0, 2) },
]

export const PID_PARAMS: JointParamDef[] = [
  { key: 'P', label: 'Пропорц.', step: 10, min: 0, max: 10000, digits: 0, defaults: ax(1000, 1000, 1200, 800) },
  { key: 'I', label: 'Интеграл.', step: 0.1, min: 0, max: 100, digits: 1, defaults: ax(0, 0, 0, 0) },
  { key: 'D', label: 'Дифференц.', step: 0.1, min: 0, max: 100, digits: 1, defaults: ax(0, 0, 0, 0) },
  { key: 'FF0', label: 'Упреждение 0', step: 0.01, min: 0, max: 10, digits: 2, defaults: ax(0, 0, 0, 0) },
  { key: 'FF1', label: 'Упреждение 1', step: 0.01, min: 0, max: 10, digits: 2, defaults: ax(1, 1, 1, 1) },
  {
    key: 'FF2',
    label: 'Упреждение 2',
    step: 0.0001,
    min: 0,
    max: 1,
    digits: 4,
    defaults: ax(0.0001, 0.0001, 0.0002, 0),
  },
  {
    key: 'DEADBAND',
    label: 'Мёртвая зона',
    unit: 'мм',
    step: 0.0001,
    min: 0,
    max: 0.1,
    digits: 4,
    defaults: ax(0.0005, 0.0005, 0.0005, 0.001),
  },
  { key: 'BIAS', label: 'Смещение', step: 0.01, min: -10, max: 10, digits: 2, defaults: ax(0, 0, 0, 0) },
  {
    key: 'MAX_OUTPUT',
    label: 'Огр. выхода',
    unit: 'мм/с',
    step: 5,
    min: 0,
    max: 1000,
    digits: 0,
    defaults: ax(300, 300, 250, 400),
  },
  {
    key: 'MAX_ERROR',
    label: 'Огр. ошибки',
    unit: 'мм',
    step: 0.01,
    min: 0,
    max: 5,
    digits: 3,
    defaults: ax(0, 0, 0, 0),
  },
]

const AXIS_JOINT: Record<Axis, number> = { X: 0, Y: 1, Z: 2, A: 3 }

export const jointGroup = (axis: Axis) => `JOINT_${AXIS_JOINT[axis]}`

const toParam = (def: JointParamDef, axis: Axis): ParamDef => {
  const { defaults, ...rest } = def
  return { ...rest, group: jointGroup(axis), id: `${axis}.${def.key}`, default: defaults[axis] }
}

export const jointParam = (def: JointParamDef, axis: Axis) => toParam(def, axis)

export type HalPinType = 'bit' | 'float' | 's32'
export type HalPinDef = { name: string; type: HalPinType; dir: 'IN' | 'OUT' }
export type HalGroup = { title: string; pins: HalPinDef[] }

const axisPins = (suffix: string, type: HalPinType, dir: 'IN' | 'OUT'): HalPinDef[] =>
  AXES.map((_, i) => ({ name: `joint.${i}.${suffix}`, type, dir }))

export const HAL_GROUPS: HalGroup[] = [
  {
    title: 'Питание и безопасность',
    pins: [
      { name: 'iocontrol.0.emc-enable-in', type: 'bit', dir: 'IN' },
      { name: 'halui.estop.is-activated', type: 'bit', dir: 'OUT' },
      { name: 'halui.machine.is-on', type: 'bit', dir: 'OUT' },
    ],
  },
  {
    title: 'Режим и программа',
    pins: [
      { name: 'halui.mode.is-auto', type: 'bit', dir: 'OUT' },
      { name: 'halui.mode.is-mdi', type: 'bit', dir: 'OUT' },
      { name: 'halui.mode.is-manual', type: 'bit', dir: 'OUT' },
      { name: 'halui.program.is-running', type: 'bit', dir: 'OUT' },
      { name: 'halui.program.is-paused', type: 'bit', dir: 'OUT' },
      { name: 'halui.program.is-idle', type: 'bit', dir: 'OUT' },
      { name: 'motion.program-line', type: 's32', dir: 'OUT' },
      { name: 'halui.feed-override.value', type: 'float', dir: 'OUT' },
    ],
  },
  {
    title: 'Движение',
    pins: [
      { name: 'motion.in-position', type: 'bit', dir: 'OUT' },
      { name: 'motion.current-vel', type: 'float', dir: 'OUT' },
      { name: 'axis.z.pos-cmd (раб.)', type: 'float', dir: 'OUT' },
    ],
  },
  { title: 'Оси: обратная связь по положению', pins: axisPins('pos-fb', 'float', 'IN') },
  { title: 'Оси: ошибка слежения', pins: axisPins('f-error', 'float', 'OUT') },
  { title: 'Оси: базирование', pins: axisPins('homed', 'bit', 'OUT') },
  {
    title: 'Шпиндель и СОЖ',
    pins: [
      { name: 'spindle.0.on', type: 'bit', dir: 'OUT' },
      { name: 'spindle.0.speed-out', type: 'float', dir: 'OUT' },
      { name: 'iocontrol.0.coolant-flood', type: 'bit', dir: 'OUT' },
    ],
  },
  {
    title: 'Инструмент',
    pins: [{ name: 'iocontrol.0.tool-number', type: 's32', dir: 'OUT' }],
  },
]

export const HAL_ROW_COUNT = HAL_GROUPS.reduce((n, g) => n + g.pins.length, 0)

export function serviceListLength(section: ServiceSection) {
  if (section === 'hal') return HAL_ROW_COUNT
  if (section === 'system') return SYSTEM_PARAMS.length
  if (section === 'linuxcnc') return LINUXCNC_PARAMS.length
  if (section === 'mesa') return MESA_PARAMS.length
  if (section === 'motion') return MOTION_PARAMS.length
  if (section === 'joints') return JOINT_PARAMS.length
  if (section === 'pid') return PID_PARAMS.length
  return 1
}

export function paramAt(section: ServiceSection, index: number, axis: Axis): ParamDef | null {
  if (section === 'system') return SYSTEM_PARAMS[index] ?? null
  if (section === 'linuxcnc') return LINUXCNC_PARAMS[index] ?? null
  if (section === 'mesa') return MESA_PARAMS[index] ?? null
  if (section === 'motion') return MOTION_PARAMS[index] ?? null
  if (section === 'joints') return JOINT_PARAMS[index] ? toParam(JOINT_PARAMS[index], axis) : null
  if (section === 'pid') return PID_PARAMS[index] ? toParam(PID_PARAMS[index], axis) : null
  return null
}

export const SERVICE_PASSWORD = '1234'

export const sectionParams = (section: ServiceSection, axis: Axis): ParamDef[] =>
  Array.from({ length: serviceListLength(section) }, (_, i) => paramAt(section, i, axis)).filter(
    (d): d is ParamDef => d !== null,
  )

export const unsavedIds = (params: Record<string, number>, saved: Record<string, number>) =>
  Object.keys(params).filter((id) => params[id] !== saved[id])

export const hasServiceList = (section: ServiceSection) => section !== 'help'

export function defaultParams(): Record<string, number> {
  const out: Record<string, number> = {}
  for (const d of [...SYSTEM_PARAMS, ...LINUXCNC_PARAMS, ...MOTION_PARAMS, ...MESA_PARAMS]) out[d.id] = d.default
  for (const d of [...JOINT_PARAMS, ...PID_PARAMS]) for (const a of AXES) out[`${a}.${d.key}`] = d.defaults[a]
  return out
}

export function formatParam(def: ParamDef, value: number) {
  if (def.options) return def.options[value] ?? String(value)
  return value.toFixed(def.digits)
}

export type { LadderContact, LadderRung } from './types'

export interface LadderBranchResult {
  closed: boolean[]
  on: boolean
}

export interface LadderRungResult {
  closed: boolean[]
  branches: LadderBranchResult[]
  /** Ток на входе катушки (хотя бы одна ветвь замкнута) */
  out: boolean
  /** Состояние выхода катушки: для таймера — бит Q, для Set/Reset — защёлка */
  on: boolean
  /** Накопленное время таймера, мс */
  acc?: number
  /** Текущее значение счётчика */
  count?: number
}

export interface LadderResult {
  signals: Record<string, boolean>
  rungs: LadderRungResult[]
  mem: LadderMem
}

export function sameLadderMem(a: LadderMem, b: LadderMem) {
  const ta = Object.keys(a.timers)
  const la = Object.keys(a.latched)
  const ca = Object.keys(a.counters)
  return (
    ta.length === Object.keys(b.timers).length &&
    la.length === Object.keys(b.latched).length &&
    ca.length === Object.keys(b.counters).length &&
    ta.every((k) => b.timers[k]?.acc === a.timers[k].acc && b.timers[k]?.q === a.timers[k].q) &&
    la.every((k) => b.latched[k] === a.latched[k]) &&
    ca.every((k) => b.counters[k]?.acc === a.counters[k].acc && b.counters[k]?.q === a.counters[k].q)
  )
}

function resolveOperand(
  x: string,
  signals: Record<string, boolean>,
  timers: LadderMem['timers'],
  counters: LadderMem['counters'],
): number {
  const t = x.replace(/\s/g, '')
  if (/^-?\d+$/.test(t)) return Number(t)
  if (/^T\d{1,2}$/.test(t)) return timers[t]?.acc ?? 0
  if (/^C\d{1,2}$/.test(t)) return counters[t]?.acc ?? 0
  return signals[t] ? 1 : 0
}

function compareValues(op: string, a: number, b: number): boolean {
  switch (op) {
    case '>':
      return a > b
    case '<':
      return a < b
    case '>=':
      return a >= b
    case '<=':
      return a <= b
    case '=':
      return a === b
    case '<>':
      return a !== b
    default:
      return false
  }
}

function evalContact(
  c: LadderContact,
  signals: Record<string, boolean>,
  timers: LadderMem['timers'],
  counters: LadderMem['counters'],
): boolean {
  if (c.cmp) {
    const a = resolveOperand(c.cmp.a, signals, timers, counters)
    const b = resolveOperand(c.cmp.b, signals, timers, counters)
    return compareValues(c.cmp.op, a, b)
  }
  return c.nc ? !signals[c.addr] : !!signals[c.addr]
}

/** Один скан ПЛК. `dt` — прошедшее время в мс (0 — только отображение без продвижения таймеров). */
export function evaluateLadder(s: MachineState, ladder: LadderRung[] = savedRungs(s), dt = 0): LadderResult {
  const signals: Record<string, boolean> = {
    'I0.0': s.run !== 'ESTOP',
    'I0.1': true,
    'I0.2': true,
    'I0.3': true,
    'I0.4': true,
    'I0.5': !s.alarms.some((a) => a.code === 1104 && a.active),
    'M0.0': s.spindleOn,
    'M0.1': s.coolant,
    'M0.2': s.run === 'RUN',
    'M0.3': s.alarms.some((a) => a.active && a.level !== 'info') || s.run === 'ALARM' || s.run === 'ESTOP',
  }
  const prev = s.ladder.mem
  const latched: Record<string, boolean> = {}
  const timers: LadderMem['timers'] = {}
  const counters: LadderMem['counters'] = {}
  Object.assign(signals, prev.latched)
  for (const rung of ladder) {
    const k = coilKind(rung.coil)
    if (isTimerKind(k)) signals[rung.coil.addr] = prev.timers[rung.coil.addr]?.q ?? false
    else if (isCounterKind(k)) signals[rung.coil.addr] = prev.counters[rung.coil.addr]?.q ?? false
  }

  const rungs = ladder.map<LadderRungResult>((rung) => {
    const branches: LadderBranchResult[] = rungBranches(rung).map((contacts) => {
      const closed = contacts.map((c) => evalContact(c, signals, timers, counters))
      return { closed, on: closed.length > 0 && closed.every(Boolean) }
    })
    const closed = branches[0]?.closed ?? []
    const out = branches.some((b) => b.on)
    const { addr } = rung.coil
    const kind = coilKind(rung.coil)

    if (isTimerKind(kind)) {
      const preset = rung.coil.preset ?? DEFAULT_PRESET_MS
      const before = prev.timers[addr]?.acc ?? (kind === 'tof' ? preset : 0)
      let acc: number
      let q: boolean
      if (kind === 'ton') {
        acc = out ? Math.min(before + dt, preset) : 0
        q = out && acc >= preset
      } else {
        acc = out ? 0 : Math.min(before + dt, preset)
        q = acc < preset
      }
      timers[addr] = { acc, q }
      signals[addr] = q
      return { closed, branches, out, on: q, acc }
    }

    if (isCounterKind(kind)) {
      const preset = rung.coil.preset ?? DEFAULT_PRESET_COUNT
      const before = prev.counters[addr]
      const acc0 = before?.acc ?? (kind === 'ctd' ? preset : 0)
      const rising = out && !(before?.prev ?? false)
      const acc = kind === 'ctu' ? Math.min(acc0 + (rising ? 1 : 0), preset) : Math.max(acc0 - (rising ? 1 : 0), 0)
      const q = kind === 'ctu' ? acc >= preset : acc <= 0
      counters[addr] = { acc, q, prev: out }
      signals[addr] = q
      return { closed, branches, out, on: q, count: acc }
    }

    if (isLatchKind(kind)) {
      const value = out ? kind === 'set' : (latched[addr] ?? prev.latched[addr] ?? false)
      latched[addr] = value
      signals[addr] = value
      return { closed, branches, out, on: value }
    }

    signals[addr] = out
    return { closed, branches, out, on: out }
  })
  return { signals, rungs, mem: { timers, counters, latched } }
}
