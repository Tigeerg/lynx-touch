'use client'

import {
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  ChevronUp,
  ChevronsDown,
  ChevronsUp,
  CornerDownLeft,
  X,
  type LucideIcon,
} from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import { MODE_LOCKED_SCREENS, TAB_LABEL, TABS, screenOfTab, tabOfScreen, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { TAB_ICON } from '../screen-tabs'
import { HwKey } from './hw-parts'

export function HardwareNav({
  state,
  dispatch,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  className?: string
}) {
  const inService = state.screen === 'service'
  const editing = state.editor !== null
  const canBeginEdit = state.editMode && state.screen === 'prog' && !state.fileDialog

  const moveCursor = (delta: number) =>
    dispatch(state.fileDialog ? { type: 'FILE_CURSOR', delta } : { type: 'CURSOR', delta })
  const mdi = state.workplace === 'SETUP'
  const mdiBusy = state.run === 'RUN' || state.run === 'HOLD'
  const mdiCanRun = mdi && !mdiBusy && state.mdi.buffer.trim() !== ''
  const mdiCanCancel = mdi && !mdiBusy && state.mdi.buffer !== ''
  const sv = state.service
  const serviceCanCancel = inService && !state.help && (sv.auth !== null || sv.editing !== null || sv.focus === 'list')
  const enter = () =>
    dispatch(
      inService && !state.help
        ? { type: 'SERVICE_ENTER' }
        : mdi
          ? { type: 'CYCLE_START' }
          : editing
            ? { type: 'EDIT_COMMIT' }
            : { type: 'EDIT_BEGIN' },
    )
  const cancel = () =>
    dispatch(
      inService && !state.help
        ? { type: 'SERVICE_CANCEL' }
        : mdi
          ? { type: 'MDI_BUFFER', value: '' }
          : { type: 'EDIT_CANCEL' },
    )

  return (
    <section
      aria-label="Экраны и навигация"
      className={cn(
        'flex min-h-0 flex-col justify-center gap-4 rounded-lg border-2 border-hw-edge bg-hw-plate p-3 shadow-inner',
        className,
      )}
    >
      <div
        role="group"
        aria-label="Вкладки экранов"
        className="flex shrink-0 justify-center gap-2 rounded-md border border-hw-edge bg-hw-edge/40 px-3 pb-3 pt-5"
      >
        {TABS.map((tab, i) => {
          const target = screenOfTab(tab, state.workplace)
          return (
            <HwKey
              key={tab}
              label={i + 1}
              icon={TAB_ICON[tab]}
              title={TAB_LABEL[tab]}
              lit={tabOfScreen(state.screen) === tab}
              disabled={MODE_LOCKED_SCREENS[state.mode].includes(target)}
              tone="primary"
              onClick={() => dispatch({ type: 'SET_SCREEN', screen: target })}
              className="font-mono"
            />
          )
        })}
      </div>

      <div
        role="group"
        aria-label="Курсор и ввод"
        className="grid shrink-0 grid-cols-[repeat(4,4.5rem)] justify-center gap-2 rounded-md border border-hw-edge bg-hw-edge/40 p-3"
      >
        <HwKey
          label="Ввод"
          icon={CornerDownLeft}
          title={inService ? 'Открыть / применить' : mdi ? 'Выполнить кадр MDI' : 'Подтвердить ввод'}
          disabled={inService && !state.help ? false : mdi ? !mdiCanRun : !editing && !canBeginEdit}
          cap="bg-hw-green text-hw-ink"
          onClick={enter}
          className="text-[12px]"
        />
        <NavKey icon={ChevronUp} title="Курсор вверх" onClick={() => moveCursor(-1)} />
        <HwKey
          label="Отмена"
          icon={X}
          title={inService ? 'Отменить / назад' : mdi ? 'Сбросить строку MDI' : 'Отменить ввод кадра'}
          disabled={inService && !state.help ? !serviceCanCancel : mdi ? !mdiCanCancel : !editing}
          cap="bg-hw-red text-hw-ink"
          onClick={cancel}
          className="text-[12px]"
        />
        <PageKey icon={ChevronsUp} label="PgUp" title="Страница вверх" onClick={() => moveCursor(-10)} />

        <NavKey
          icon={ChevronLeft}
          title="Влево / значение −"
          disabled={!inService}
          onClick={() => dispatch({ type: 'PARAM_ADJUST', dir: -1 })}
        />
        <NavKey icon={ChevronDown} title="Курсор вниз" onClick={() => moveCursor(1)} />
        <NavKey
          icon={ChevronRight}
          title="Вправо / значение +"
          disabled={!inService}
          onClick={() => dispatch({ type: 'PARAM_ADJUST', dir: 1 })}
        />
        <PageKey icon={ChevronsDown} label="PgDn" title="Страница вниз" onClick={() => moveCursor(10)} />
      </div>
    </section>
  )
}

function PageKey({
  icon,
  label,
  title,
  onClick,
}: {
  icon: LucideIcon
  label: string
  title: string
  onClick: () => void
}) {
  return (
    <HwKey
      label={label}
      icon={icon}
      title={title}
      cap="bg-hw-blue text-hw-ink"
      onClick={onClick}
      className="text-[10px]"
    />
  )
}

function NavKey({
  icon: Icon,
  title,
  disabled,
  onClick,
}: {
  icon: LucideIcon
  title: string
  disabled?: boolean
  onClick: () => void
}) {
  return (
    <HwKey label={<Icon className="size-8" aria-label={title} />} title={title} disabled={disabled} onClick={onClick} />
  )
}
