// Navigation subsystem: the mode, the workplace and the screen, the cursor over the active
// screen list, and the program position (rewind and start from a block). Extracted from
// machine.ts after the pattern of ladder.ts and prog.ts: the reducer composes the subsystems,
// and the size of the machine is held by the tools/check.ps1 mark. A few cases fall back to
// the whole machine (a screen change, the mode cycle, the cursor handing PARAM_ADJUST /
// SERVICE_SECTION / LADDER_PICK_MOVE up), so the caller passes its reducer as dispatch. A
// result is { s, msg, level }; machine.ts shows the message.
import { applyBlock } from './cycle'
import { MODE_LABEL, SCREEN_LABEL, WORKPLACE_LABEL } from './data'
import { currentFile, programLines } from './files'
import { isBusy } from './guards'
import { listLength } from './list'
import type { Action } from './machine'
import { notify } from './messages'
import { PROBE_CYCLES } from './probe'
import { idleSim } from './prog'
import { cancelEdit } from './service-edit'
import {
  MODE_LOCKED_SCREENS,
  MODE_START_SCREEN,
  MODES,
  MPG_STEPS,
  SERVICE_SECTIONS,
  screenOfTab,
  tabOfScreen,
  type MachineState,
  type Message,
  type Mode,
  type Screen,
  type Workplace,
} from './types'

const clamp = (v: number, min: number, max: number) => Math.min(max, Math.max(min, v))

export type NavAction =
  | { type: 'SET_MODE'; mode: Mode }
  | { type: 'SET_WORKPLACE'; workplace: Workplace }
  | { type: 'CYCLE_MODE'; dir: 1 | -1 }
  | { type: 'SET_SCREEN'; screen: Screen }
  | { type: 'CURSOR'; delta: number }
  | { type: 'PROG_REWIND' }
  | { type: 'PROG_TO_CURSOR' }

const NAV_TYPES = new Set<string>([
  'SET_MODE',
  'SET_WORKPLACE',
  'CYCLE_MODE',
  'SET_SCREEN',
  'CURSOR',
  'PROG_REWIND',
  'PROG_TO_CURSOR',
])

export const isNavAction = (a: { type: string }): a is NavAction => NAV_TYPES.has(a.type)

export type Dispatch = (s: MachineState, a: Action) => MachineState

export type Result = { s: MachineState; msg?: string; level?: Message['level'] }

export function navReducer(s: MachineState, a: NavAction, dispatch: Dispatch): Result {
  switch (a.type) {
    case 'SET_MODE': {
      if (s.run === 'RUN') return { s, msg: 'Смена режима во время выполнения запрещена', level: 'warn' }
      const screen = MODE_START_SCREEN[a.mode]
      const jogStep = a.mode === 'MPG' && !MPG_STEPS.includes(s.jogStep) ? MPG_STEPS[MPG_STEPS.length - 1] : s.jogStep
      const n = s.editMode && screen !== 'prog' ? { ...s, editMode: false, editor: null } : s
      return { s: { ...n, mode: a.mode, screen, jogStep }, msg: `Режим: ${MODE_LABEL[a.mode]}` }
    }

    case 'SET_WORKPLACE': {
      if (isBusy(s)) return { s, msg: 'Смена режима во время выполнения запрещена', level: 'warn' }
      const mode: Mode = a.workplace === 'SETUP' ? 'JOG' : 'AUTO'
      const kept = screenOfTab(tabOfScreen(s.screen), a.workplace)
      const screen = MODE_LOCKED_SCREENS[mode].includes(kept) ? screenOfTab('work', a.workplace) : kept
      return {
        s: { ...s, workplace: a.workplace, mode, dryRun: a.workplace === 'TEST', screen },
        msg: `Режим: ${WORKPLACE_LABEL[a.workplace]}`,
      }
    }

    case 'CYCLE_MODE': {
      const idx = (MODES.indexOf(s.mode) + a.dir + MODES.length) % MODES.length
      return { s: dispatch(s, { type: 'SET_MODE', mode: MODES[idx] }) }
    }

    case 'SET_SCREEN': {
      if (MODE_LOCKED_SCREENS[s.mode].includes(a.screen)) {
        return {
          s,
          msg: `В режиме «${WORKPLACE_LABEL[s.workplace]}» экран «${SCREEN_LABEL[a.screen]}» заблокирован`,
          level: 'warn',
        }
      }
      if (a.screen !== 'service' && (s.service.editing || s.service.auth)) {
        const reverted = cancelEdit(s)
        return { s: dispatch({ ...reverted, service: { ...reverted.service, auth: null } }, a) }
      }
      if (a.screen !== 'prog' && s.editMode) {
        return {
          s: { ...s, screen: a.screen, editMode: false, editor: null },
          msg: `Файл ${currentFile(s).name} сохранён`,
        }
      }
      return { s: { ...s, screen: a.screen } }
    }

    case 'CURSOR': {
      if (s.screen === 'offset' && s.probe.open && s.probe.step === 0) {
        return {
          s: {
            ...s,
            probe: { ...s.probe, cycle: clamp(s.probe.cycle + Math.sign(a.delta), 0, PROBE_CYCLES.length - 1) },
          },
        }
      }
      if (s.screen === 'service' && !s.help) {
        const sv = s.service
        if (sv.auth) return { s }
        if (sv.editing) return { s: dispatch(s, { type: 'PARAM_ADJUST', dir: a.delta < 0 ? 1 : -1, coarse: true }) }
        if (sv.focus === 'nav') {
          const i = SERVICE_SECTIONS.indexOf(sv.section)
          return {
            s: dispatch(s, {
              type: 'SERVICE_SECTION',
              section: SERVICE_SECTIONS[clamp(i + a.delta, 0, SERVICE_SECTIONS.length - 1)],
            }),
          }
        }
      }
      if (s.screen === 'service' && s.service.section === 'ladder') {
        if (s.ladder.form) return { s }
        if (s.ladder.manager) return { s: dispatch(s, { type: 'LADDER_PICK_MOVE', delta: a.delta }) }
      }
      const len = listLength(s, s.screen)
      return { s: { ...s, cursor: { ...s.cursor, [s.screen]: clamp(s.cursor[s.screen] + a.delta, 0, len - 1) } } }
    }

    case 'PROG_REWIND':
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      return {
        s: { ...s, line: 0, nextLine: 0, sim: idleSim(), cursor: { ...s.cursor, prog: 0 } },
        msg: 'Программа в начале',
      }

    case 'PROG_TO_CURSOR': {
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      const idx = s.cursor.prog
      let base: MachineState = {
        ...s,
        sim: idleSim(),
        motion: null,
        dwell: 0,
        homing: false,
        stopAfterBlock: false,
        programEnd: false,
        message: null,
      }
      for (let i = 0; i < idx; i++) base = applyBlock(base, programLines(s)[i], false)
      const codes = [base.offsets[base.activeOffset].code]
      if (base.spindleOn) codes.push(`S${base.spindleSet} ${base.spindleDir === 1 ? 'M3' : 'M4'}`)
      if (base.coolant) codes.push('M8')
      return {
        s: {
          ...base,
          line: idx,
          nextLine: idx,
          dwell: 0,
          motion: null,
          homing: false,
          stopAfterBlock: false,
          programEnd: false,
          message: null,
        },
        msg: `Старт с кадра ${idx + 1}: восстановлено ${codes.join(', ')}`,
      }
    }
  }
}
