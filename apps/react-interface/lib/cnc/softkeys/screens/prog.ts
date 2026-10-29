// The program screen: the editor form, the edit mode with the line keys, or the plain program.
import {
  ArrowRightToLine,
  Check,
  ChevronDown,
  ChevronUp,
  Eye,
  FolderOpen,
  ListOrdered,
  ListPlus,
  ListX,
  Pencil,
  Play,
  Save,
  Search,
  SkipBack,
  SquarePen,
  X,
} from 'lucide-react'
import { TOOLPATH_PARAM } from '../../service-data'
import type { MachineState } from '../../types'
import { slots, type Softkey } from '../softkey-slots'

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
