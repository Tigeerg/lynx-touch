// The softkeys of the windows that open over any screen: the file catalogue, the tool call, the
// calculator, the search and the tool measuring. A window decides the whole bar while it is
// open, so these sets are checked before the workplace and the screen (see getSoftkeys).
import { CornerDownRight, FolderOpen, Play, Replace, Search, X } from 'lucide-react'
import type { MachineState } from '../types'
import { slots, type Softkey } from './softkey-slots'

export const fileDialog = (): (Softkey | null)[] =>
  slots([
    { label: 'Открыть', icon: FolderOpen, action: { type: 'FILE_OPEN' } },
    { label: 'Закрыть', icon: X, action: { type: 'FILE_DIALOG', open: false } },
  ])

export const toolDialog = (): (Softkey | null)[] =>
  slots([
    { label: 'Вызвать', icon: CornerDownRight, action: { type: 'TOOL_RUN' } },
    { label: 'Отмена', icon: X, action: { type: 'TOOL_DIALOG', open: false } },
  ])

export const calc = (): (Softkey | null)[] =>
  slots([{ label: 'Закрыть', icon: X, action: { type: 'CALC', open: false }, active: true }])

/** The search window: replacing inside a running program is refused, hence `busy`. */
export const search = (s: MachineState, busy: boolean): (Softkey | null)[] =>
  slots([
    { label: 'Найти далее', icon: Search, action: { type: 'SEARCH_FIND' } },
    { label: 'Заменить всё', icon: Replace, action: { type: 'SEARCH_REPLACE_ALL' }, disabled: busy },
    { label: 'Закрыть', icon: X, action: { type: 'SEARCH', open: false }, active: true },
  ])

/** The tool measuring window; the run itself is started and stopped by these two keys. */
export const toolMeasure = (s: MachineState): (Softkey | null)[] =>
  slots([
    {
      label: s.toolMeasure.length === null ? 'Пуск замера' : 'Повторить',
      icon: Play,
      action: { type: 'TOOL_MEASURE_START' },
      disabled: s.toolMeasure.running,
    },
    { label: s.toolMeasure.running ? 'Прервать' : 'Закрыть', icon: X, action: { type: 'TOOL_MEASURE_CLOSE' } },
  ])
