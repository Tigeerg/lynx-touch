import {
  ArrowRightToLine,
  Calculator,
  Check,
  CircleHelp,
  Crosshair,
  Eraser,
  Eye,
  EyeOff,
  Flame,
  FolderOpen,
  ListOrdered,
  ListPlus,
  ListX,
  Pencil,
  Package,
  RefreshCcw,
  Save,
  ScanSearch,
  Shovel,
  SquarePen,
  X,
  Ruler,
  SkipBack,
  Table,
  Trash2,
  Wrench,
  Lock,
  LockOpen,
  MonitorCog,
  Route,
  Axis3d,
  SlidersHorizontal,
  Cable,
  Workflow,
  FileCog,
  CircuitBoard,
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  ChevronUp,
  CornerDownRight,
  House,
  Minus,
  Play,
  Repeat,
  Replace,
  Search,
  LocateFixed,
  RotateCcw,
  Upload,
  FilePlus2,
  Copy,
  Plus,
  ToggleLeft,
  Equal,
  GitBranch,
  Undo2,
  type LucideIcon,
} from 'lucide-react'
import { PROBE_STEP_MEASURE, PROBE_STEP_RESULT, PROBE_TOOL } from './probe'
import type { Action } from './machine'
import { defaultParams, paramAt, TOOLPATH_PARAM, unsavedIds } from './service-data'
import type { MachineState, ServiceSection } from './types'

export const SERVICE_ICON: Record<ServiceSection, LucideIcon> = {
  system: MonitorCog,
  linuxcnc: FileCog,
  motion: Route,
  joints: Axis3d,
  pid: SlidersHorizontal,
  mesa: CircuitBoard,
  hal: Cable,
  ladder: Workflow,
  help: CircleHelp,
}

export const SOFTKEY_SLOTS = 8

export interface Softkey {
  label: string
  icon: LucideIcon
  action: Action
  active?: boolean
  disabled?: boolean
}

export function getSoftkeys(s: MachineState): (Softkey | null)[] {
  const busy = s.run === 'RUN' || s.run === 'HOLD'

  let keys: (Softkey | null)[] = []

  if (s.fileDialog) {
    keys = [
      { label: 'Открыть', icon: FolderOpen, action: { type: 'FILE_OPEN' } },
      { label: 'Закрыть', icon: X, action: { type: 'FILE_DIALOG', open: false } },
    ]
    return [...keys, ...Array<null>(SOFTKEY_SLOTS - keys.length).fill(null)]
  }

  if (s.toolDialog) {
    keys = [
      { label: 'Вызвать', icon: CornerDownRight, action: { type: 'TOOL_RUN' } },
      { label: 'Отмена', icon: X, action: { type: 'TOOL_DIALOG', open: false } },
    ]
    return [...keys, ...Array<null>(SOFTKEY_SLOTS - keys.length).fill(null)]
  }

  if (s.calc) {
    keys = [{ label: 'Закрыть', icon: X, action: { type: 'CALC', open: false }, active: true }]
    return [...keys, ...Array<null>(SOFTKEY_SLOTS - keys.length).fill(null)]
  }

  if (s.search.open) {
    keys = [
      { label: 'Найти далее', icon: Search, action: { type: 'SEARCH_FIND' } },
      { label: 'Заменить всё', icon: Replace, action: { type: 'SEARCH_REPLACE_ALL' }, disabled: busy },
      { label: 'Закрыть', icon: X, action: { type: 'SEARCH', open: false }, active: true },
    ]
    return [...keys, ...Array<null>(SOFTKEY_SLOTS - keys.length).fill(null)]
  }

  // Рабочее место «НАЛАДКА»: набор F1-F8 для ручной подготовки (обмер, привязка
  // баз, парковка) на стартовом экране «Позиция». Экраны с собственным мастером
  // (смещения, сервис, редактор) сохраняют свои софт-кеи.
  if (s.workplace === 'SETUP' && s.screen === 'pos') {
    keys = [
      {
        label: 'Обмер инструмента',
        icon: Ruler,
        action: { type: 'TOOL_PROBE' },
        disabled: busy || s.spindleOn || s.tool === PROBE_TOOL,
      },
      { label: 'Центроискатель', icon: LocateFixed, action: { type: 'PROBE_OPEN', open: true }, disabled: busy },
      { label: 'Запись базы', icon: Save, action: { type: 'OFFSET_TEACH', axis: s.jogAxis }, disabled: busy },
      { label: 'Таблица баз', icon: Table, action: { type: 'SET_SCREEN', screen: 'offset' } },
      {
        label: 'Парковка',
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
    ]
    return [...keys, ...Array<null>(SOFTKEY_SLOTS - keys.length).fill(null)]
  }

  // Рабочее место «ПРОГОН»: проверка УП без движения осей. Набор F1-F8 ведёт по
  // каталогу, правке, поиску, прогону, габаритам, очистке графики, виду и шторке.
  if (s.workplace === 'TEST' && !s.editMode) {
    keys = [
      { label: 'Каталог УП', icon: FolderOpen, action: { type: 'FILE_DIALOG', open: true }, disabled: busy },
      { label: 'Редактор', icon: SquarePen, action: { type: 'EDIT_MODE', on: true }, disabled: busy },
      { label: 'Поиск / Замена', icon: Search, action: { type: 'SEARCH', open: true } },
      {
        label: s.sim.active ? 'Прогон: стоп' : 'Мат. прогон',
        icon: Play,
        action: { type: 'SIM' },
        active: s.sim.active,
        disabled: busy || s.run === 'ESTOP' || s.run === 'ALARM',
      },
      { label: 'Габариты УП', icon: Ruler, action: { type: 'TEST_BBOX' }, active: s.test.bbox },
      { label: 'Очистка графики', icon: EyeOff, action: { type: 'TEST_CLEAR_PATH' }, active: s.test.hidePath },
      { label: 'Вид', icon: Eye, action: { type: 'PATH_VIEW_CYCLE' } },
      { label: 'Скрыть код', icon: Eye, action: { type: 'TEST_CURTAIN' }, active: s.test.curtain },
    ]
    return [...keys, ...Array<null>(SOFTKEY_SLOTS - keys.length).fill(null)]
  }

  // Рабочее место «АВТО»: набор F1-F8 для боевой обработки на экране программы.
  // Резерв (F7) оставлен пустым под макрос пользователя.
  if (s.workplace === 'RUN' && s.screen === 'prog' && !s.editMode) {
    keys = [
      { label: 'Каталог УП', icon: FolderOpen, action: { type: 'FILE_DIALOG', open: true }, disabled: busy },
      { label: 'Старт с кадра', icon: ArrowRightToLine, action: { type: 'PROG_TO_CURSOR' }, disabled: busy },
      { label: 'Ресурс инструмента', icon: Wrench, action: { type: 'TOOL_LIFE' } },
      {
        label: s.chipConveyor ? 'Шнек: стоп' : 'Шнек стружки',
        icon: Shovel,
        action: { type: 'TOGGLE', key: 'chipConveyor' },
        active: s.chipConveyor,
      },
      { label: 'Счётчики деталей', icon: Package, action: { type: 'PART_COUNTER_RESET' } },
      { label: 'Проверка слома', icon: ScanSearch, action: { type: 'BROKEN_TOOL_CHECK' } },
      null,
      {
        label: s.params[TOOLPATH_PARAM] === 1 ? 'Вид: текст' : 'Вид: графика',
        icon: Eye,
        action: { type: 'TOOLPATH_TOGGLE' },
        active: s.params[TOOLPATH_PARAM] === 1,
      },
    ]
    return [...keys, ...Array<null>(SOFTKEY_SLOTS - keys.length).fill(null)]
  }

  switch (s.screen) {
    case 'pos':
      keys = [
        {
          label: 'Замер инструмента',
          icon: Ruler,
          action: { type: 'TOOL_PROBE' },
          disabled: busy || s.spindleOn || s.tool === PROBE_TOOL,
        },
        { label: 'Поиск детали', icon: LocateFixed, action: { type: 'PROBE_OPEN', open: true }, disabled: busy },
        {
          label: `СК ${s.offsets[s.activeOffset].code}`,
          icon: Repeat,
          action: { type: 'OFFSET_NEXT' },
          disabled: busy,
        },
        {
          label: 'Безопасный отвод',
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
        {
          label: 'Зажим оси A',
          icon: s.aBrake ? Lock : LockOpen,
          action: { type: 'TOGGLE', key: 'aBrake' },
          active: s.aBrake,
        },
        { label: 'Вызов инструмента', icon: Wrench, action: { type: 'TOOL_DIALOG', open: true }, disabled: busy },
        { label: 'Калькулятор режимов', icon: Calculator, action: { type: 'CALC', open: true }, active: s.calc },
      ]
      break
    case 'prog':
      if (s.editor) {
        keys = [
          { label: 'Применить', icon: Check, action: { type: 'EDIT_COMMIT' } },
          { label: 'Отмена', icon: X, action: { type: 'EDIT_CANCEL' } },
        ]
        break
      }
      if (s.editMode) {
        keys = [
          { label: 'Изменить кадр', icon: Pencil, action: { type: 'EDIT_BEGIN' } },
          { label: 'Вставить кадр', icon: ListPlus, action: { type: 'EDIT_INSERT' } },
          { label: 'Удалить кадр', icon: ListX, action: { type: 'EDIT_DELETE' } },
          { label: 'Кадр выше', icon: ChevronUp, action: { type: 'CURSOR', delta: -1 } },
          { label: 'Кадр ниже', icon: ChevronDown, action: { type: 'CURSOR', delta: 1 } },
          null,
          { label: 'Сохранить и выйти', icon: Save, action: { type: 'EDIT_MODE', on: false }, active: true },
        ]
        break
      }
      keys = [
        { label: 'Каталог / USB', icon: FolderOpen, action: { type: 'FILE_DIALOG', open: true }, disabled: busy },
        { label: 'Редактировать', icon: SquarePen, action: { type: 'EDIT_MODE', on: true }, disabled: busy },
        { label: 'Поиск / Замена', icon: Search, action: { type: 'SEARCH', open: true } },
        {
          label: s.sim.active ? 'Симуляция: стоп' : 'Симуляция',
          icon: Play,
          action: { type: 'SIM' },
          active: s.sim.active,
          disabled: busy || s.run === 'ESTOP' || s.run === 'ALARM',
        },
        { label: 'Перенумерация', icon: ListOrdered, action: { type: 'PROG_RENUMBER' }, disabled: busy },
        { label: 'В начало УП', icon: SkipBack, action: { type: 'PROG_REWIND' }, disabled: busy },
        { label: 'Старт с кадра', icon: ArrowRightToLine, action: { type: 'PROG_TO_CURSOR' }, disabled: busy },
        {
          label: s.params[TOOLPATH_PARAM] === 1 ? 'Вид: текст' : 'Вид: графика',
          icon: Eye,
          action: { type: 'TOOLPATH_TOGGLE' },
          active: s.params[TOOLPATH_PARAM] === 1,
        },
      ]
      break
    case 'offset':
      if (s.probe.open) {
        const p = s.probe
        const atResult = p.step === PROBE_STEP_RESULT
        keys = [
          {
            label: 'Назад',
            icon: ChevronLeft,
            action: { type: 'PROBE_STEP', dir: -1 },
            disabled: p.running || p.step === 0,
          },
          {
            label: 'Далее',
            icon: ChevronRight,
            action: { type: 'PROBE_STEP', dir: 1 },
            disabled: p.running || p.step >= PROBE_STEP_MEASURE || (p.step === 1 && s.tool !== PROBE_TOOL),
          },
          {
            label: 'Щуп T99',
            icon: LocateFixed,
            action: { type: 'PROBE_CALL_TOOL' },
            active: s.tool === PROBE_TOOL,
            disabled: busy,
          },
          {
            label: `Записать в ${s.offsets[s.cursor.offset].code}`,
            icon: Save,
            action: { type: 'PROBE_WRITE' },
            active: p.written,
            disabled: !atResult || !p.result,
          },
          { label: 'Повторить', icon: RotateCcw, action: { type: 'PROBE_RETRY' }, disabled: !atResult },
          {
            label: 'Измерить',
            icon: Play,
            action: { type: 'CYCLE_START' },
            active: p.running,
            disabled:
              p.running ||
              p.step !== PROBE_STEP_MEASURE ||
              s.tool !== PROBE_TOOL ||
              s.spindleOn ||
              s.run === 'ESTOP' ||
              s.run === 'ALARM',
          },
          {
            label: 'Закрыть щуп',
            icon: X,
            action: { type: 'PROBE_OPEN', open: false },
            active: true,
            disabled: p.running,
          },
        ]
        break
      }
      keys = [
        { label: 'Активировать', icon: Check, action: { type: 'OFFSET_ACTIVATE' }, disabled: busy },
        { label: 'Привязка X', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'X' }, disabled: busy },
        { label: 'Привязка Y', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'Y' }, disabled: busy },
        { label: 'Привязка Z', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'Z' }, disabled: busy },
        { label: 'Привязка A', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'A' }, disabled: busy },
        {
          label: `Обнулить ${s.offsets[s.cursor.offset].code}`,
          icon: Eraser,
          action: { type: 'OFFSET_CLEAR' },
          disabled: busy,
        },
        { label: '3D щуп', icon: LocateFixed, action: { type: 'PROBE_OPEN', open: true }, disabled: busy },
        {
          label: 'Обнулить по позиции',
          icon: Crosshair,
          action: { type: 'ZERO_WORK' },
          disabled: s.mode !== 'JOG' || busy,
        },
      ]
      break
    case 'tool':
      keys = [
        {
          label: 'Вызвать в шпиндель',
          icon: Wrench,
          action: { type: 'TOOL_CALL' },
          disabled: s.mode === 'AUTO' || busy,
        },
        { label: 'Замер длины', icon: Ruler, action: { type: 'TOOL_MEASURE' }, disabled: busy },
        { label: 'Сброс ресурса', icon: RefreshCcw, action: { type: 'TOOL_RESET_LIFE' } },
        {
          label: s.toolClamped ? 'Отжать' : 'Зажать',
          icon: s.toolClamped ? LockOpen : Lock,
          action: { type: 'TOOL_CLAMP', clamp: !s.toolClamped },
          active: s.toolClamped,
          disabled: s.toolClamped && (s.spindleOn || busy),
        },
        null,
        null,
        null,
        null,
      ]
      break
    case 'service': {
      const L = s.ladder
      if (s.service.section === 'ladder' && L.form) {
        keys = [
          { label: 'Применить', icon: Check, action: { type: 'LADDER_FORM_COMMIT' } },
          { label: 'Отмена', icon: X, action: { type: 'LADDER_FORM_CANCEL' } },
        ]
        break
      }
      if (s.service.section === 'ladder' && L.manager) {
        const picked = L.pick
        keys = [
          { label: 'Загрузить в ПЛК', icon: Upload, action: { type: 'LADDER_LOAD' }, disabled: L.dirty || busy },
          { label: 'Новая схема', icon: FilePlus2, action: { type: 'LADDER_NEW' }, disabled: L.dirty || busy },
          { label: 'Копия', icon: Copy, action: { type: 'LADDER_COPY' } },
          { label: 'Переименовать', icon: Pencil, action: { type: 'LADDER_RENAME' } },
          {
            label: 'Удалить схему',
            icon: Trash2,
            action: { type: 'LADDER_REMOVE' },
            disabled: picked === L.active || L.schemes.length === 1,
          },
          { label: 'Схема выше', icon: ChevronUp, action: { type: 'LADDER_PICK_MOVE', delta: -1 } },
          { label: 'Схема ниже', icon: ChevronDown, action: { type: 'LADDER_PICK_MOVE', delta: 1 } },
          { label: 'Закрыть', icon: X, action: { type: 'LADDER_MANAGER', open: false }, active: true },
        ]
        break
      }
      if (s.service.section === 'ladder' && L.draft) {
        const row = Math.min(s.cursor.service, L.draft.length - 1)
        const onCoil = s.ladder.path === 0 && s.ladder.col === L.draft[row]?.contacts.length
        keys = [
          { label: 'Контакт +', icon: Plus, action: { type: 'LADDER_ADD_CONTACT' } },
          { label: 'Сравнение +', icon: Equal, action: { type: 'LADDER_ADD_COMPARE' } },
          { label: 'Ветвь +', icon: GitBranch, action: { type: 'LADDER_ADD_BRANCH' } },
          { label: onCoil ? 'Тип катушки' : 'НО / НЗ', icon: ToggleLeft, action: { type: 'LADDER_TOGGLE_NC' } },
          { label: 'Ступень +', icon: ListPlus, action: { type: 'LADDER_ADD_RUNG' } },
          { label: 'Схемы', icon: FolderOpen, action: { type: 'LADDER_MANAGER', open: true } },
          { label: 'Сохранить', icon: Save, action: { type: 'LADDER_SAVE' }, active: L.dirty, disabled: busy },
          { label: 'Удалить элемент', icon: Trash2, action: { type: 'LADDER_DELETE' } },
        ]
        break
      }
      const sv = s.service
      const unsaved = unsavedIds(s.params, sv.saved).length
      const focused =
        sv.focus === 'list' && !sv.editing && !sv.auth ? paramAt(sv.section, s.cursor.service, sv.axis) : null
      const canReset =
        !!focused && s.params[focused.id] !== defaultParams()[focused.id] && (focused.free || (sv.write && !busy))
      keys = [
        { label: 'Назад', icon: Undo2, action: { type: 'SERVICE_BACK' } },
        {
          label: sv.write ? 'Блок. записи' : 'Разрешить запись',
          icon: sv.write ? LockOpen : Lock,
          action: { type: 'SERVICE_LOCK' },
          active: sv.write,
          disabled: !sv.write && busy,
        },
        {
          label: unsaved > 0 ? `Сохранить (${unsaved})` : 'Сохранить',
          icon: Save,
          action: { type: 'SERVICE_SAVE' },
          active: unsaved > 0,
          disabled: !sv.write || busy,
        },
        {
          label: 'По умолчанию',
          icon: RotateCcw,
          action: { type: 'SERVICE_DEFAULTS' },
          disabled: !canReset,
        },
        sv.section === 'ladder'
          ? { label: 'Правка схемы', icon: SquarePen, action: { type: 'LADDER_EDIT', on: true } }
          : null,
        {
          label: sv.editing ? 'Применить' : sv.focus === 'nav' ? 'К списку' : 'Изменить',
          icon: CornerDownRight,
          action: { type: 'SERVICE_ENTER' },
        },
        {
          label: sv.editing ? 'Значение −' : sv.focus === 'nav' ? 'Раздел выше' : 'Позиция выше',
          icon: sv.editing ? Minus : ChevronUp,
          action: sv.editing ? { type: 'PARAM_ADJUST', dir: -1 } : { type: 'CURSOR', delta: -1 },
          disabled: !!sv.auth,
        },
        {
          label: sv.editing ? 'Значение +' : sv.focus === 'nav' ? 'Раздел ниже' : 'Позиция ниже',
          icon: sv.editing ? Plus : ChevronDown,
          action: sv.editing ? { type: 'PARAM_ADJUST', dir: 1 } : { type: 'CURSOR', delta: 1 },
          disabled: !!sv.auth,
        },
      ]
      break
    }
    case 'alarm':
      keys = [
        {
          label: 'Квитировать',
          icon: Check,
          action: { type: 'ALARM_ACK' },
          disabled: !s.alarms[s.cursor.alarm]?.active,
        },
        { label: 'Удалить квитир.', icon: Trash2, action: { type: 'ALARM_CLEAR_ALL' } },
        { label: 'Сброс', icon: RotateCcw, action: { type: 'RESET' }, disabled: s.run === 'ESTOP' },
        null,
        null,
        null,
        null,
        null,
      ]
      break
  }

  return [...keys.slice(0, SOFTKEY_SLOTS), ...Array<null>(Math.max(0, SOFTKEY_SLOTS - keys.length)).fill(null)]
}
