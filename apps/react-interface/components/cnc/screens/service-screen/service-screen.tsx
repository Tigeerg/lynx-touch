import { useEffect, useMemo, useRef, useState } from 'react'
import { Lock, LockOpen, PanelLeftClose, PanelLeftOpen, RotateCcw, Settings } from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import {
  INI_FILE,
  JOINT_PARAMS,
  jointParam,
  LINUXCNC_PARAMS,
  MESA_PARAMS,
  MOTION_PARAMS,
  PID_PARAMS,
  SERVICE_HINT,
  SERVICE_LABEL,
  SYSTEM_PARAMS,
  unsavedIds,
} from '@/lib/cnc/service-data'
import { SERVICE_ICON } from '@/lib/cnc/softkeys'
import { SERVICE_PAGE_LABEL, SERVICE_PAGES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { HelpGroups } from '../../help-overlay'
import { Panel } from '../../primitives'
import { LadderView } from '../ladder-view'
import { HalMonitor } from './hal-monitor'
import { MesaIo } from './mesa-io'
import { AxisSelector, ParamTable } from './params-table'
import { PasswordDialog } from './password-dialog'
import { InfoStrip, SystemInfo, linuxcncInfo, mesaInfo } from './system-info'

export function ServiceScreen({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const { section, write, restartPending, focus, saved } = state.service
  const unsaved = useMemo(() => unsavedIds(state.params, saved).length, [state.params, saved])
  // The per-axis rows are rebuilt into new objects on every render otherwise: 18 of them for the
  // joint table alone, while only the axis changes their content.
  const jointDefs = useMemo(() => JOINT_PARAMS.map((d) => jointParam(d, state.service.axis)), [state.service.axis])
  const pidDefs = useMemo(() => PID_PARAMS.map((d) => jointParam(d, state.service.axis)), [state.service.axis])
  const activeNavRef = useRef<HTMLButtonElement>(null)
  // The section list starts as a narrow icon strip: on the 1080 portrait touch panel the full list
  // took a third of the width from the parameter table.
  const [collapsed, setCollapsed] = useState(true)
  const [prevFocus, setPrevFocus] = useState(focus)
  if (focus !== prevFocus) {
    setPrevFocus(focus)
    if (focus === 'list') setCollapsed(true)
  }
  useEffect(() => {
    activeNavRef.current?.scrollIntoView({ block: 'nearest' })
  }, [section])
  return (
    <Panel
      title={`Сервис · ${SERVICE_LABEL[section]}`}
      icon={Settings}
      className="h-full"
      right={
        <>
          {unsaved > 0 && (
            <span className="rounded-sm border border-primary/50 bg-primary/10 px-2 py-0.5 text-xs font-bold text-primary">
              Не сохранено: {unsaved}
            </span>
          )}
          {restartPending && (
            <span className="flex items-center gap-1 rounded-sm bg-warning px-2 py-0.5 text-xs font-bold text-warning-foreground">
              <RotateCcw className="size-3.5" aria-hidden="true" />
              Нужен перезапуск
            </span>
          )}
          <span className="font-mono text-xs text-muted-foreground">{INI_FILE}</span>
          <span
            className={cn(
              'flex items-center gap-1 rounded-sm px-2 py-0.5 text-xs font-bold',
              write ? 'bg-warning text-warning-foreground' : 'border text-muted-foreground',
            )}
          >
            {write ? (
              <LockOpen className="size-3.5" aria-hidden="true" />
            ) : (
              <Lock className="size-3.5" aria-hidden="true" />
            )}
            {write ? 'ЗАПИСЬ' : 'ЧТЕНИЕ'}
          </span>
        </>
      }
    >
      <div className="flex h-full min-h-0">
        <nav
          id="service-nav"
          aria-label="Разделы сервиса"
          className={cn(
            'flex min-h-0 shrink-0 flex-col border-r bg-muted/50 transition-[width] duration-200 ease-out motion-reduce:transition-none',
            collapsed ? 'w-14' : 'w-64',
          )}
        >
          <button
            type="button"
            tabIndex={-1}
            onClick={() => setCollapsed((c) => !c)}
            aria-expanded={!collapsed}
            aria-controls="service-nav"
            aria-label={collapsed ? 'Развернуть разделы' : 'Свернуть разделы'}
            title={collapsed ? 'Развернуть разделы' : 'Свернуть разделы'}
            className={cn(
              'flex h-11 shrink-0 items-center gap-2 border-b px-2 text-xs font-bold uppercase tracking-wider text-muted-foreground hover:bg-accent hover:text-accent-foreground',
              collapsed ? 'justify-center' : 'justify-between pl-3',
            )}
          >
            {!collapsed && <span>Разделы</span>}
            {collapsed ? (
              <PanelLeftOpen className="size-5" aria-hidden="true" />
            ) : (
              <PanelLeftClose className="size-5" aria-hidden="true" />
            )}
          </button>
          <div
            className={cn(
              'flex min-h-0 flex-1 flex-col gap-0.5 overflow-y-auto overflow-x-hidden',
              collapsed ? 'p-1.5' : 'p-2',
            )}
          >
            {SERVICE_PAGES.map((sections, pageIndex) => (
              <div
                key={pageIndex}
                role="group"
                aria-label={SERVICE_PAGE_LABEL[pageIndex]}
                className="flex flex-col gap-0.5"
              >
                {collapsed ? (
                  pageIndex > 0 && <hr className="mx-1.5 my-1.5 border-border" />
                ) : (
                  <p className="truncate px-1 pb-0.5 pt-1.5 text-[10px] font-bold uppercase tracking-wider text-muted-foreground">
                    {SERVICE_PAGE_LABEL[pageIndex]}
                  </p>
                )}
                {sections.map((sec) => {
                  const Icon = SERVICE_ICON[sec]
                  const active = sec === section
                  const navFocused = focus === 'nav'
                  return (
                    <button
                      key={sec}
                      ref={active ? activeNavRef : undefined}
                      type="button"
                      tabIndex={-1}
                      aria-current={active ? 'true' : undefined}
                      aria-label={collapsed ? SERVICE_LABEL[sec] : undefined}
                      title={collapsed ? `${SERVICE_LABEL[sec]} — ${SERVICE_HINT[sec]}` : undefined}
                      onClick={() => {
                        dispatch({ type: 'SERVICE_SECTION', section: sec })
                        setCollapsed(true)
                      }}
                      className={cn(
                        'flex items-center rounded-sm border text-left',
                        collapsed ? 'h-12 justify-center' : 'gap-2.5 px-2 py-1.5',
                        active && navFocused && 'border-primary bg-primary text-primary-foreground',
                        active && !navFocused && 'border-primary/50 bg-accent text-accent-foreground',
                        !active && 'border-transparent hover:bg-accent/60',
                      )}
                    >
                      <Icon className={cn('shrink-0', collapsed ? 'size-6' : 'size-5')} aria-hidden="true" />
                      {!collapsed && (
                        <span className="min-w-0 leading-tight">
                          <span className="block truncate text-sm font-semibold">{SERVICE_LABEL[sec]}</span>
                          <span
                            className={cn(
                              'block truncate text-[11px]',
                              active && navFocused ? 'text-primary-foreground/80' : 'text-muted-foreground',
                            )}
                          >
                            {SERVICE_HINT[sec]}
                          </span>
                        </span>
                      )}
                    </button>
                  )
                })}
              </div>
            ))}
          </div>
        </nav>

        <div className="relative flex min-h-0 min-w-0 flex-1 flex-col">
          <PasswordDialog state={state} dispatch={dispatch} />
          {section === 'system' && (
            <>
              <SystemInfo />
              <ParamTable state={state} defs={SYSTEM_PARAMS} />
            </>
          )}
          {section === 'linuxcnc' && (
            <>
              <InfoStrip items={linuxcncInfo(state)} />
              <ParamTable state={state} defs={LINUXCNC_PARAMS} />
            </>
          )}
          {section === 'mesa' && (
            <>
              <InfoStrip items={mesaInfo(state)} />
              <MesaIo state={state} />
              <ParamTable state={state} defs={MESA_PARAMS} />
            </>
          )}
          {section === 'motion' && <ParamTable state={state} defs={MOTION_PARAMS} />}
          {section === 'joints' && (
            <>
              <AxisSelector state={state} />
              <ParamTable state={state} defs={jointDefs} />
            </>
          )}
          {section === 'pid' && (
            <>
              <AxisSelector state={state} />
              <ParamTable state={state} defs={pidDefs} />
            </>
          )}
          {section === 'hal' && <HalMonitor state={state} />}
          {section === 'ladder' && <LadderView state={state} dispatch={dispatch} />}
          {section === 'help' && (
            <div className="min-h-0 flex-1 overflow-y-auto">
              <HelpGroups className="gap-3 p-3" />
            </div>
          )}
        </div>
      </div>
    </Panel>
  )
}
