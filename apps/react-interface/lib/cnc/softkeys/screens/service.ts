// The service screen: ladder form, the scheme manager, the ladder draft, or the parameter list.
import {
  Check,
  ChevronDown,
  ChevronUp,
  Copy,
  CornerDownRight,
  Equal,
  FilePlus2,
  FolderOpen,
  GitBranch,
  ListPlus,
  Lock,
  LockOpen,
  Minus,
  Pencil,
  Plus,
  RotateCcw,
  Save,
  SquarePen,
  ToggleLeft,
  Trash2,
  Undo2,
  Upload,
  X,
} from 'lucide-react'
import { paramAt, paramValue, unsavedIds } from '../../service-data'
import { isParamSection, type MachineState } from '../../types'
import { slots, type Softkey } from '../softkey-slots'

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
