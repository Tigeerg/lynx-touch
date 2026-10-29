// The screen sets: `switch (s.screen)` of getSoftkeys. Six screens, each with its own bar, some
// with an inner mode (editor, probe wizard, ladder form/manager/draft) that replaces the whole
// set. The two helpers below the imports are the offset code the labels read.
import {
  ArrowRightToLine,
  Calculator,
  Check,
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  ChevronUp,
  CircleHelp,
  Copy,
  CornerDownRight,
  Crosshair,
  Equal,
  Eraser,
  Eye,
  FilePlus2,
  FolderOpen,
  GitBranch,
  House,
  ListOrdered,
  ListPlus,
  ListX,
  LocateFixed,
  Lock,
  LockOpen,
  Minus,
  Pencil,
  Play,
  Plus,
  RefreshCcw,
  Repeat,
  RotateCcw,
  Ruler,
  Save,
  Search,
  SkipBack,
  SquarePen,
  ToggleLeft,
  Trash2,
  Undo2,
  Upload,
  Wrench,
  X,
} from 'lucide-react'
import { SOFTKEY_LABEL } from '../data'
import { PROBE_STEP_MEASURE, PROBE_STEP_RESULT, PROBE_TOOL } from '../probe'
import { paramAt, paramValue, TOOLPATH_PARAM, unsavedIds } from '../service-data'
import { isParamSection, type MachineState } from '../types'
import { slots, type Softkey } from './softkey-slots'

/**
 * The code of the offset under the cursor. The key must render even when the list is empty (the
 * cursor reads an element that is not there), so an absent row shows a dash instead of taking
 * the bar down.
 */
const offsetCode = (s: MachineState) => s.offsets[s.cursor.offset]?.code ?? '—'

// The Position screen: probing, the base pick, moving to a known point and the calculator.
export const posScreen = (s: MachineState, busy: boolean): (Softkey | null)[] =>
  slots([
    {
      label: SOFTKEY_LABEL.TOOL_PROBE,
      icon: Ruler,
      action: { type: 'TOOL_PROBE' },
      disabled: busy || s.spindleOn || s.tool === PROBE_TOOL,
    },
    {
      label: SOFTKEY_LABEL.PROBE_OPEN,
      icon: LocateFixed,
      action: { type: 'PROBE_OPEN', open: true },
      disabled: busy,
    },
    {
      label: `СК ${s.offsets[s.activeOffset].code}`,
      icon: Repeat,
      action: { type: 'OFFSET_NEXT' },
      disabled: busy,
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
    {
      label: 'Зажим оси A',
      icon: s.aBrake ? Lock : LockOpen,
      action: { type: 'TOGGLE', key: 'aBrake' },
      active: s.aBrake,
    },
    { label: 'Вызов инструмента', icon: Wrench, action: { type: 'TOOL_DIALOG', open: true }, disabled: busy },
    { label: 'Калькулятор режимов', icon: Calculator, action: { type: 'CALC', open: true }, active: s.calc },
  ])

// The program screen: the editor form, the edit mode with the line keys, or the plain program.
export const progScreen = (s: MachineState, busy: boolean): (Softkey | null)[] => {
  if (s.editor) {
    return slots([
      { label: 'Применить', icon: Check, action: { type: 'EDIT_COMMIT' } },
      { label: 'Отмена', icon: X, action: { type: 'EDIT_CANCEL' } },
    ])
  }
  if (s.editMode) {
    return slots([
      { label: 'Изменить кадр', icon: Pencil, action: { type: 'EDIT_BEGIN' } },
      { label: 'Вставить кадр', icon: ListPlus, action: { type: 'EDIT_INSERT' } },
      { label: 'Удалить кадр', icon: ListX, action: { type: 'EDIT_DELETE' } },
      { label: 'Кадр выше', icon: ChevronUp, action: { type: 'CURSOR', delta: -1 } },
      { label: 'Кадр ниже', icon: ChevronDown, action: { type: 'CURSOR', delta: 1 } },
      null,
      { label: 'Сохранить и выйти', icon: Save, action: { type: 'EDIT_MODE', on: false }, active: true },
    ])
  }
  return slots([
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
      label: s.params[TOOLPATH_PARAM] === 1 ? 'Экран: текст' : 'Экран: графика',
      icon: Eye,
      action: { type: 'TOOLPATH_TOGGLE' },
      active: s.params[TOOLPATH_PARAM] === 1,
    },
  ])
}

// The offsets screen: the probe wizard while it is open, the base table otherwise.
export const offsetScreen = (s: MachineState, busy: boolean): (Softkey | null)[] => {
  if (s.probe.open) {
    const p = s.probe
    const atResult = p.step === PROBE_STEP_RESULT
    return slots([
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
        label: `Записать в ${offsetCode(s)}`,
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
    ])
  }
  return slots([
    { label: 'Активировать', icon: Check, action: { type: 'OFFSET_ACTIVATE' }, disabled: busy },
    { label: 'Привязка X', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'X' }, disabled: busy },
    { label: 'Привязка Y', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'Y' }, disabled: busy },
    { label: 'Привязка Z', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'Z' }, disabled: busy },
    { label: 'Привязка A', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'A' }, disabled: busy },
    {
      label: `Обнулить ${offsetCode(s)}`,
      icon: Eraser,
      action: { type: 'OFFSET_CLEAR' },
      disabled: busy,
    },
    {
      label: SOFTKEY_LABEL.PROBE_OPEN,
      icon: LocateFixed,
      action: { type: 'PROBE_OPEN', open: true },
      disabled: busy,
    },
    {
      label: 'Обнулить по позиции',
      icon: Crosshair,
      action: { type: 'ZERO_WORK' },
      disabled: s.mode !== 'JOG' || busy,
    },
  ])
}

// The tool screen: tool change, length measure, life reset, clamp.
export const toolScreen = (s: MachineState, busy: boolean): (Softkey | null)[] =>
  slots([
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
  ])

// The service screen: ladder form, the scheme manager, the ladder draft, or the parameter list.
export const serviceScreen = (s: MachineState, busy: boolean): (Softkey | null)[] => {
  const L = s.ladder
  if (s.service.section === 'ladder' && L.form) {
    return slots([
      { label: 'Применить', icon: Check, action: { type: 'LADDER_FORM_COMMIT' } },
      { label: 'Отмена', icon: X, action: { type: 'LADDER_FORM_CANCEL' } },
    ])
  }
  if (s.service.section === 'ladder' && L.manager) {
    const picked = L.pick
    return slots([
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
    ])
  }
  if (s.service.section === 'ladder' && L.draft) {
    const row = Math.min(s.cursor.service, L.draft.length - 1)
    const onCoil = s.ladder.path === 0 && s.ladder.col === L.draft[row]?.contacts.length
    return slots([
      { label: 'Контакт +', icon: Plus, action: { type: 'LADDER_ADD_CONTACT' } },
      { label: 'Сравнение +', icon: Equal, action: { type: 'LADDER_ADD_COMPARE' } },
      { label: 'Ветвь +', icon: GitBranch, action: { type: 'LADDER_ADD_BRANCH' } },
      { label: onCoil ? 'Тип катушки' : 'НО / НЗ', icon: ToggleLeft, action: { type: 'LADDER_TOGGLE_NC' } },
      { label: 'Ступень +', icon: ListPlus, action: { type: 'LADDER_ADD_RUNG' } },
      { label: 'Схемы', icon: FolderOpen, action: { type: 'LADDER_MANAGER', open: true } },
      { label: 'Сохранить', icon: Save, action: { type: 'LADDER_SAVE' }, active: L.dirty, disabled: busy },
      { label: 'Удалить элемент', icon: Trash2, action: { type: 'LADDER_DELETE' } },
    ])
  }
  const sv = s.service
  const unsaved = unsavedIds(s.params, sv.saved).length
  const focused =
    sv.focus === 'list' && !sv.editing && !sv.auth && isParamSection(sv.section)
      ? paramAt(sv.section, s.cursor.service, sv.axis)
      : null
  const canReset =
    !!focused && paramValue(s.params, focused.id) !== focused.default && (focused.free || (sv.write && !busy))
  return slots([
    { label: 'Назад', icon: Undo2, action: { type: 'SERVICE_BACK' } },
    {
      label: sv.write ? 'Блокировать запись' : 'Разрешить запись',
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
  ])
}

// The alarm screen: acknowledge, clear the acknowledged, reset.
export const alarmScreen = (s: MachineState): (Softkey | null)[] =>
  slots([
    {
      label: 'Квитировать',
      icon: Check,
      action: { type: 'ALARM_ACK' },
      disabled: !s.alarms[s.cursor.alarm]?.active,
    },
    { label: 'Удалить квитированные', icon: Trash2, action: { type: 'ALARM_CLEAR_ALL' } },
    { label: 'Сброс', icon: RotateCcw, action: { type: 'RESET' }, disabled: s.run === 'ESTOP' },
    null,
    null,
    null,
    null,
    null,
  ])
