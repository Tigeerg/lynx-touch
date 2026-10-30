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
  Wand2,
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
    { label: 'С кадра', icon: ArrowRightToLine, action: { type: 'PROG_TO_CURSOR' }, disabled: busy },
    // The quick program opens as an overlay of this screen (PLAN-QUICKPROG, 7.4): no sixth tab. The
    // bar is eight slots, so the new key takes the eighth place, and while the overlay is open the
    // same place closes it: the close key never slides the bar. The view switch takes that place
    // back only when the overlay is closed, so the operator loses nothing by opening the form.
    s.cam
      ? { label: 'Закрыть', icon: X, action: { type: 'CAM_CLOSE' }, active: true }
      : { label: 'Быстрая УП', icon: Wand2, action: { type: 'CAM_OPEN' }, disabled: busy },
  ])
}
