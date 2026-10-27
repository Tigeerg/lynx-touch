'use client'

import { useEffect, useMemo, useRef, useState } from 'react'
import {
  ChevronLeft,
  ChevronRight,
  KeyRound,
  Lock,
  LockOpen,
  PanelLeftClose,
  PanelLeftOpen,
  RotateCcw,
  Settings,
} from 'lucide-react'
import type { Action } from '@/lib/cnc/machine'
import {
  formatParam,
  formatValue,
  HAL_GROUPS,
  INI_FILE,
  JOINT_PARAMS,
  jointGroup,
  jointParam,
  LINUXCNC_PARAMS,
  MESA_BOARD_PARAM,
  MESA_IPADDR_PARAM,
  MESA_PARAMS,
  MOTION_PARAMS,
  paramValue,
  PID_PARAMS,
  SERVICE_HINT,
  SERVICE_LABEL,
  SERVICE_PASSWORD,
  SYSTEM_PARAMS,
  unsavedIds,
  type HalPinType,
  type ParamDef,
} from '@/lib/cnc/service-data'
import { LadderView } from './ladder-view'
import { halValues } from '@/lib/cnc/hal'
import { SERVICE_ICON } from '@/lib/cnc/softkeys'
import { AXES, SERVICE_PAGE_LABEL, SERVICE_PAGES, type MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { HelpGroups } from '../help-overlay'
import { Keycap, Panel } from '../primitives'

/** The Windows IME processing key code: a keydown carrying it is composition, not a command. */
const IME_KEYCODE = 229

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

function PasswordDialog({ state, dispatch }: { state: MachineState; dispatch: (a: Action) => void }) {
  const auth = state.service.auth
  if (!auth) return null
  return (
    <div className="absolute inset-0 z-20 flex items-center justify-center bg-background/80 backdrop-blur-[1px]">
      <form
        role="dialog"
        aria-label="Доступ к записи параметров"
        onSubmit={(e) => e.preventDefault()}
        className="flex w-80 flex-col gap-3 rounded-md border-2 border-primary bg-card p-4 shadow-lg"
      >
        <div className="flex items-center gap-2">
          <KeyRound className="size-5 text-primary" aria-hidden="true" />
          <h3 className="text-base font-bold">Доступ к записи</h3>
        </div>
        <label className="flex flex-col gap-1 text-xs font-semibold uppercase tracking-wider text-muted-foreground">
          Пароль
          <input
            type="password"
            autoFocus
            autoComplete="off"
            value={auth.input}
            aria-invalid={auth.error}
            onChange={(e) => dispatch({ type: 'SERVICE_AUTH_INPUT', value: e.target.value })}
            onKeyDown={(e) => {
              if (e.nativeEvent.isComposing || e.nativeEvent.keyCode === IME_KEYCODE) return
              if (e.key === 'Enter') {
                e.preventDefault()
                dispatch({ type: 'SERVICE_ENTER' })
              } else if (e.key === 'Escape') {
                e.preventDefault()
                dispatch({ type: 'SERVICE_CANCEL' })
              }
            }}
            className={cn(
              'h-10 rounded-sm border bg-background px-3 font-mono text-lg tracking-widest text-foreground outline-none focus-visible:ring-2 focus-visible:ring-primary',
              auth.error && 'border-destructive',
            )}
          />
        </label>
        <p
          role="status"
          className={cn('min-h-4 text-xs', auth.error ? 'font-semibold text-destructive' : 'text-muted-foreground')}
        >
          {auth.error ? 'Неверный пароль' : `Демо-пароль: ${SERVICE_PASSWORD}`}
        </p>
        <div className="flex gap-2">
          <button
            type="button"
            onClick={() => dispatch({ type: 'SERVICE_ENTER' })}
            className="h-9 flex-1 rounded-sm bg-primary text-sm font-bold text-primary-foreground"
          >
            Ввод
          </button>
          <button
            type="button"
            onClick={() => dispatch({ type: 'SERVICE_CANCEL' })}
            className="h-9 flex-1 rounded-sm border bg-muted text-sm font-bold"
          >
            Отмена
          </button>
        </div>
      </form>
    </div>
  )
}

const SYSTEM_INFO = [
  ['LinuxCNC', '2.9.4'],
  ['Ядро RT', '6.1.0-rt PREEMPT'],
  ['Servo-поток', '1.000 мс'],
  ['Джиттер', '11.8 мкс'],
  ['Интерфейс', 'Mesa 7i96S'],
  ['IP-адрес', '192.168.1.121'],
]

function SystemInfo() {
  return <InfoStrip items={SYSTEM_INFO.map(([label, value]) => ({ label, value }))} />
}

type InfoItem = { label: string; value: string; tone?: 'ok' | 'warn' }

function InfoStrip({ items }: { items: InfoItem[] }) {
  return (
    <dl className="grid shrink-0 grid-cols-3 gap-px xl:grid-cols-6 border-b bg-border">
      {items.map(({ label, value, tone }) => (
        <div key={label} className="bg-card px-3 py-2">
          <dt className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">{label}</dt>
          <dd className="flex items-center gap-1.5 font-mono text-sm font-bold">
            {tone && (
              <span
                aria-hidden="true"
                className={cn('size-2 rounded-full', tone === 'ok' ? 'bg-success' : 'bg-warning')}
              />
            )}
            {value}
          </dd>
        </div>
      ))}
    </dl>
  )
}

function linuxcncInfo(s: MachineState): InfoItem[] {
  const on = s.run !== 'ESTOP'
  return [
    { label: 'Конфигурация', value: INI_FILE },
    { label: 'Task', value: 'milltask' },
    { label: 'Интерпретатор', value: 'rs274ngc' },
    { label: 'Motion', value: on ? 'ENABLED' : 'DISABLED', tone: on ? 'ok' : 'warn' },
    { label: 'Файл параметров', value: 'linuxcnc.var' },
    { label: 'Состояние task', value: s.run === 'RUN' ? 'EXEC' : s.run === 'HOLD' ? 'PAUSED' : 'IDLE' },
  ]
}

/** The text of an option row of the catalogue, looked up by key rather than by row position. */
function optionText(s: MachineState, id: string): string {
  const def = MESA_PARAMS.find((d) => d.id === id)
  return def?.options?.[paramValue(s.params, id)] ?? '—'
}

function mesaInfo(s: MachineState): InfoItem[] {
  const linked = s.run !== 'ESTOP'
  return [
    { label: 'Плата', value: optionText(s, MESA_BOARD_PARAM) },
    { label: 'Драйвер', value: 'hm2_eth' },
    { label: 'IP-адрес', value: optionText(s, MESA_IPADDR_PARAM) },
    { label: 'Связь', value: linked ? '100 Мбит/с' : 'Нет', tone: linked ? 'ok' : 'warn' },
    { label: 'Прошивка', value: '7i96s_d.bin' },
    { label: 'Watchdog', value: linked ? 'OK · 0 сраб.' : 'BITE', tone: linked ? 'ok' : 'warn' },
  ]
}

/** The Mesa terminal wiring: `addr: null` is an unwired terminal, not an empty address. */
type MesaTerminal = { terminal: string; addr: string | null }

const MESA_INPUTS: MesaTerminal[] = [
  { terminal: 'IN0', addr: 'I0.0' },
  { terminal: 'IN1', addr: 'I0.1' },
  { terminal: 'IN2', addr: 'I0.2' },
  { terminal: 'IN3', addr: 'I0.3' },
  { terminal: 'IN4', addr: 'I0.4' },
  { terminal: 'IN5', addr: 'I0.5' },
  { terminal: 'IN6', addr: null },
  { terminal: 'IN7', addr: null },
  { terminal: 'IN8', addr: null },
  { terminal: 'IN9', addr: null },
  { terminal: 'IN10', addr: null },
]

const MESA_OUTPUTS: MesaTerminal[] = [
  { terminal: 'OUT0', addr: 'Q0.0' },
  { terminal: 'OUT1', addr: 'Q0.1' },
  { terminal: 'OUT2', addr: 'Q0.2' },
  { terminal: 'OUT3', addr: 'Q0.3' },
  { terminal: 'OUT4', addr: 'Q0.4' },
  { terminal: 'OUT5', addr: 'Q0.5' },
]

function IoLed({ name, on }: { name: string; on: boolean }) {
  return (
    <span className="flex flex-col items-center gap-0.5">
      <span
        aria-label={`${name}: ${on ? 'вкл' : 'выкл'}`}
        className={cn('size-3.5 rounded-sm border', on ? 'border-success bg-success' : 'bg-muted')}
      />
      <span className="font-mono text-[10px] text-muted-foreground">{name}</span>
    </span>
  )
}

function MesaIo({ state }: { state: MachineState }) {
  // The signals come from the last TICK scan (state.ladder.signals): scanning again here was a
  // second evaluateLadder in the same render as the one in the cycle reducer.
  const signals = state.ladder.signals
  return (
    <div className="flex shrink-0 items-center gap-6 border-b px-3 py-2">
      <div className="flex items-center gap-3">
        <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Входы TB3</span>
        <div className="flex gap-2">
          {MESA_INPUTS.map(({ terminal, addr }) => (
            <IoLed key={terminal} name={terminal} on={addr !== null && !!signals[addr]} />
          ))}
        </div>
      </div>
      <div className="flex items-center gap-3">
        <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Выходы TB4</span>
        <div className="flex gap-2">
          {MESA_OUTPUTS.map(({ terminal, addr }) => (
            <IoLed key={terminal} name={terminal} on={addr !== null && !!signals[addr]} />
          ))}
        </div>
      </div>
      <span className="ml-auto font-mono text-[11px] text-muted-foreground">hm2_7i96s.0 · цикл чтения 142 мкс</span>
    </div>
  )
}

function AxisSelector({ state }: { state: MachineState }) {
  return (
    <div className="flex shrink-0 items-center gap-2 border-b px-3 py-2">
      <span className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">Ось</span>
      {AXES.map((a) => {
        const active = a === state.service.axis
        return (
          <span
            key={a}
            aria-current={active ? 'true' : undefined}
            className={cn(
              'flex h-8 items-center gap-2 rounded-sm border px-2.5 font-mono text-sm font-bold',
              active ? 'border-primary bg-primary text-primary-foreground' : 'bg-muted',
            )}
          >
            {a}
            <span
              className={cn(
                'font-sans text-[11px] font-medium',
                active ? 'text-primary-foreground/80' : 'text-muted-foreground',
              )}
            >
              {jointGroup(a)}
            </span>
          </span>
        )
      })}
      <span className="ml-auto flex items-center gap-1.5 text-xs text-muted-foreground">
        <Keycap>←</Keycap>
        <Keycap>→</Keycap>
        выбор оси
      </span>
    </div>
  )
}

const HEADER_CELL = 'px-3 py-1.5 text-left text-[11px] font-semibold uppercase tracking-wider text-muted-foreground'

function ParamTable({ state, defs }: { state: MachineState; defs: readonly ParamDef[] }) {
  const cursor = state.cursor.service
  const selectedRef = useRef<HTMLTableRowElement>(null)
  const { section, axis, focus, editing, saved, write } = state.service

  useEffect(() => {
    selectedRef.current?.scrollIntoView({ block: 'nearest' })
  }, [cursor, section, axis, focus])

  return (
    <div className="min-h-0 flex-1 overflow-y-auto">
      <table className="w-full table-fixed border-collapse">
        <thead className="sticky top-0 z-10">
          <tr className="border-b bg-card">
            <th className={cn(HEADER_CELL, 'w-[31%]')}>Параметр</th>
            <th className={cn(HEADER_CELL, 'w-[24%]')}>Ключ INI</th>
            <th className={cn(HEADER_CELL, 'w-[25%] text-center')}>Значение</th>
            <th className={cn(HEADER_CELL, 'w-[20%] text-right')}>По умолч.</th>
          </tr>
        </thead>
        <tbody>
          {defs.map((def, i) => {
            const value = paramValue(state.params, def.id)
            const selected = i === cursor && focus === 'list'
            const isEditing = editing?.id === def.id
            const changed = value !== def.default
            const unsaved = value !== saved[def.id]
            const locked = !def.free && !write
            const defaultValue = formatValue(def, def.default)
            return (
              <tr
                key={def.id}
                ref={selected ? selectedRef : undefined}
                aria-selected={selected}
                className={cn(
                  'scroll-mt-8 border-b',
                  selected && 'bg-accent outline-2 -outline-offset-2 outline-primary',
                  isEditing && 'bg-primary/10 outline-warning',
                )}
              >
                <td className="px-3 py-2">
                  <div className="flex min-w-0 items-center gap-2">
                    <span className="line-clamp-2 min-w-0 text-xs font-semibold leading-snug" title={def.label}>
                      {def.label}
                    </span>
                    <span className="ml-auto flex w-16 shrink-0 items-center justify-end gap-1.5">
                      {def.restart && (
                        <RotateCcw
                          className="size-3.5 shrink-0 text-muted-foreground"
                          aria-label="Требует перезапуска"
                        />
                      )}
                      {locked ? (
                        <Lock className="size-3.5 shrink-0 text-muted-foreground" aria-label="Запись заблокирована" />
                      ) : (
                        <LockOpen className="size-3.5 shrink-0 text-success" aria-label="Доступно для записи" />
                      )}
                      {unsaved ? (
                        <span
                          role="img"
                          aria-label="Не сохранено"
                          title="Не сохранено"
                          className="size-2 shrink-0 rounded-full bg-warning"
                        />
                      ) : (
                        <span className="size-2 shrink-0" aria-hidden="true" />
                      )}
                    </span>
                  </div>
                </td>
                <td
                  className="px-3 py-2 font-mono text-[11px] text-muted-foreground"
                  title={`[${def.group}] ${def.key}`}
                >
                  <span className="line-clamp-2 break-words">
                    [{def.group}] {def.key}
                  </span>
                </td>
                <td className="px-3 py-1.5">
                  <div className="flex items-center justify-center gap-1">
                    <ChevronLeft
                      className={cn('size-4 shrink-0', isEditing ? 'text-warning' : 'text-transparent')}
                      aria-hidden="true"
                    />
                    <span
                      className={cn(
                        'min-w-24 rounded-sm border px-2 py-0.5 text-center font-mono text-sm font-bold tabular-nums',
                        changed ? 'border-primary/40 bg-primary/10 text-primary' : 'bg-muted',
                        isEditing && 'border-warning bg-warning text-warning-foreground ring-2 ring-warning/40',
                      )}
                    >
                      {formatParam(def, state.params)}
                      {def.unit && (
                        <span
                          className={cn(
                            'ml-1 font-sans text-[11px] font-medium',
                            isEditing ? 'text-warning-foreground/80' : 'text-muted-foreground',
                          )}
                        >
                          {def.unit}
                        </span>
                      )}
                    </span>
                    <ChevronRight
                      className={cn('size-4 shrink-0', isEditing ? 'text-warning' : 'text-transparent')}
                      aria-hidden="true"
                    />
                  </div>
                </td>
                <td
                  className="px-3 py-2 text-right font-mono text-xs tabular-nums text-muted-foreground"
                  title={defaultValue}
                >
                  <span className="line-clamp-2 break-words">{defaultValue}</span>
                </td>
              </tr>
            )
          })}
        </tbody>
      </table>
    </div>
  )
}

function PinValue({ type, value }: { type: HalPinType; value: boolean | number }) {
  if (type === 'bit') {
    return (
      <span
        className={cn(
          'inline-flex w-14 justify-center rounded-sm px-1.5 py-0.5 font-mono text-xs font-bold',
          value ? 'bg-success text-primary-foreground' : 'bg-muted text-muted-foreground',
        )}
      >
        {value ? 'TRUE' : 'FALSE'}
      </span>
    )
  }
  const n = Number(value)
  return <span className="font-mono text-sm font-semibold tabular-nums">{type === 's32' ? n : n.toFixed(4)}</span>
}

function HalMonitor({ state }: { state: MachineState }) {
  const cursor = state.cursor.service
  const selectedRef = useRef<HTMLTableRowElement>(null)
  const listFocused = state.service.focus === 'list'
  const values = halValues(state)

  useEffect(() => {
    selectedRef.current?.scrollIntoView({ block: 'nearest' })
  }, [cursor, listFocused])

  let rowIndex = 0
  return (
    <div className="min-h-0 flex-1 overflow-y-auto">
      <table className="w-full border-collapse">
        <thead className="sticky top-0 z-10">
          <tr className="border-b bg-card">
            <th className={HEADER_CELL}>Пин HAL</th>
            <th className={cn(HEADER_CELL, 'w-20')}>Тип</th>
            <th className={cn(HEADER_CELL, 'w-20')}>Напр.</th>
            <th className={cn(HEADER_CELL, 'w-32 text-right')}>Значение</th>
          </tr>
        </thead>
        {HAL_GROUPS.map((group) => (
          <tbody key={group.title}>
            <tr className="border-b bg-muted">
              <th
                colSpan={4}
                scope="colgroup"
                className="px-3 py-1.5 text-left text-xs font-bold uppercase tracking-wider text-foreground"
              >
                {group.title}
                <span className="ml-2 font-mono font-normal normal-case text-muted-foreground">
                  {group.pins.length}
                </span>
              </th>
            </tr>
            {group.pins.map((pin) => {
              const selected = rowIndex === cursor && listFocused
              rowIndex += 1
              return (
                <tr
                  key={pin.name}
                  ref={selected ? selectedRef : undefined}
                  aria-selected={selected}
                  className={cn(
                    'scroll-mt-8 border-b',
                    selected && 'bg-accent outline-2 -outline-offset-2 outline-primary',
                  )}
                >
                  <td className="px-3 py-1.5 font-mono text-xs">
                    {pin.name}
                    {pin.note && <span className="ml-2 font-sans text-muted-foreground">{pin.note}</span>}
                  </td>
                  <td className="px-3 py-1.5 font-mono text-xs text-muted-foreground">{pin.type}</td>
                  <td className="px-3 py-1.5 font-mono text-xs text-muted-foreground">{pin.dir}</td>
                  <td className="px-3 py-1.5 text-right">
                    <PinValue type={pin.type} value={values[pin.name]} />
                  </td>
                </tr>
              )
            })}
          </tbody>
        ))}
      </table>
    </div>
  )
}
