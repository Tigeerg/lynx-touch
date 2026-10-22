// Manual machine control subsystem: jog and step, overrides, the operator switches
// (coolant, dry run, machine lock, ...), the manual spindle, the tool clamp, lubrication,
// the safe-point move and the MDI buffer. Extracted from machine.ts after the pattern of
// ladder.ts and prog.ts: the reducer composes the subsystems, and the size of the machine
// is held by the tools/check.ps1 mark. A result is { s, msg, level }; machine.ts shows the
// message.
import { clamp, clampIndex } from './clamp'
import { RAPID_JOG_MULT, RAPID_STEPS, SOFT_LIMITS } from './data'
import { MACHINE_LOCK_MSG, blockedFromMotion, isBusy } from './guards'
import { PROBE_TOOL } from './probe'
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM } from './service-data'
import type { Result } from './subsystem'
import { MPG_STEPS, type Axis, type MachineState } from './types'

export type ControlToggleKey =
  | 'singleBlock'
  | 'optStop'
  | 'dryRun'
  | 'blockSkip'
  | 'machineLock'
  | 'coolant'
  | 'mist'
  | 'workLight'
  | 'chipConveyor'
  | 'jogRapid'
  | 'aBrake'

export type ControlAction =
  | { type: 'SELECT_AXIS'; axis: Axis }
  | { type: 'JOG'; dir: 1 | -1; axis?: Axis }
  | { type: 'SET_STEP'; step: number }
  | { type: 'SET_JOG_FEED'; feed: number }
  | { type: 'OVERRIDE'; kind: 'feed' | 'spindle' | 'rapid'; dir: 1 | -1 }
  | { type: 'TOGGLE'; key: ControlToggleKey }
  | { type: 'SPINDLE_STOP' }
  | { type: 'SPINDLE_MANUAL'; dir?: 1 | -1 }
  | { type: 'TOOL_CLAMP'; clamp: boolean }
  | { type: 'LUBE' }
  | { type: 'GO_SAFE' }
  | { type: 'MDI_BUFFER'; value: string }
  | { type: 'MDI_RECALL'; index: number }

const CONTROL_TYPES = new Set<string>([
  'SELECT_AXIS',
  'JOG',
  'SET_STEP',
  'SET_JOG_FEED',
  'OVERRIDE',
  'TOGGLE',
  'SPINDLE_STOP',
  'SPINDLE_MANUAL',
  'TOOL_CLAMP',
  'LUBE',
  'GO_SAFE',
  'MDI_BUFFER',
  'MDI_RECALL',
])

export const isControlAction = (a: { type: string }): a is ControlAction => CONTROL_TYPES.has(a.type)

export function controlReducer(s: MachineState, a: ControlAction): Result {
  switch (a.type) {
    case 'SELECT_AXIS':
      return { s: { ...s, jogAxis: a.axis } }

    case 'JOG': {
      if (s.mode !== 'JOG' && s.mode !== 'MPG')
        return { s, msg: 'Перемещение доступно в режиме РУЧНОЙ или МАХОВИК', level: 'warn' }
      if (s.run !== 'IDLE') return { s, msg: 'Перемещение заблокировано', level: 'warn' }
      if (s.machineLock) return { s, msg: MACHINE_LOCK_MSG, level: 'warn' }
      const ax = a.axis ?? s.jogAxis
      if (ax === 'A' && s.aBrake) return { s, msg: 'Тормоз оси A включён — снимите его клавишей F6', level: 'error' }
      const distance = s.jogRapid ? s.jogStep * RAPID_JOG_MULT : s.jogStep
      const next = Math.round((s.machine[ax] + a.dir * distance) * 1000) / 1000
      const [min, max] = SOFT_LIMITS[ax]
      if (next < min || next > max) {
        return { s: { ...s, jogAxis: ax }, msg: `Программный предел оси ${ax}${a.dir > 0 ? '+' : '−'}`, level: 'error' }
      }
      return { s: { ...s, jogAxis: ax, machine: { ...s.machine, [ax]: next } } }
    }

    case 'SET_STEP':
      if (s.mode !== 'JOG' && s.mode !== 'MPG')
        return { s, msg: 'Шаг выбирается в режиме РУЧНОЙ или МАХОВИК', level: 'warn' }
      if (s.mode === 'MPG' && !MPG_STEPS.includes(a.step))
        return { s, msg: 'Маховик: доступны множители ×1, ×10, ×100', level: 'warn' }
      return { s: { ...s, jogStep: a.step } }

    case 'SET_JOG_FEED':
      if (s.mode !== 'JOG') return { s, msg: 'Скорость перемещения задаётся в режиме РУЧНОЙ', level: 'warn' }
      return { s: { ...s, jogFeed: a.feed } }

    case 'OVERRIDE': {
      if (a.kind === 'feed')
        return { s: { ...s, feedOverride: clamp(s.feedOverride + a.dir * 10, 0, s.params[MAX_FEED_PARAM]) } }
      if (a.kind === 'spindle')
        return { s: { ...s, spindleOverride: clamp(s.spindleOverride + a.dir * 10, 50, s.params[MAX_SPINDLE_PARAM]) } }
      const idx = clampIndex(RAPID_STEPS.indexOf(s.rapidOverride), a.dir, RAPID_STEPS.length)
      return { s: { ...s, rapidOverride: RAPID_STEPS[idx] } }
    }

    case 'TOGGLE':
      if (a.key === 'dryRun' && isBusy(s))
        return { s, msg: 'Холостой ход переключается только в останове', level: 'warn' }
      if (a.key === 'machineLock' && isBusy(s))
        return { s, msg: 'Блокировка станка переключается только в останове', level: 'warn' }
      return { s: { ...s, [a.key]: !s[a.key] } }

    case 'SPINDLE_STOP':
      if (!s.spindleOn) return { s }
      if (isBusy(s)) return { s, msg: 'Остановка шпинделя во время цикла недоступна', level: 'warn' }
      return { s: { ...s, spindleOn: false }, msg: 'Шпиндель остановлен (M5)' }

    case 'SPINDLE_MANUAL': {
      if (s.mode === 'AUTO' || isBusy(s) || s.run === 'ESTOP') {
        return { s, msg: 'Ручное управление шпинделем недоступно', level: 'warn' }
      }
      const dir = a.dir ?? s.spindleDir
      if (s.spindleOn) {
        if (dir !== s.spindleDir) return { s, msg: 'Сначала остановите шпиндель', level: 'warn' }
        return { s: { ...s, spindleOn: false }, msg: 'Шпиндель остановлен' }
      }
      if (!s.toolClamped) return { s, msg: 'Инструмент разжат — пуск шпинделя запрещён', level: 'error' }
      if (s.tool === PROBE_TOOL) return { s, msg: 'В шпинделе щуп T99 — вращение запрещено', level: 'error' }
      return {
        s: { ...s, spindleOn: true, spindleDir: dir, spindleSet: s.spindleSet || 1000 },
        msg: dir > 0 ? 'Шпиндель по часовой (M3)' : 'Шпиндель против часовой (M4)',
      }
    }

    case 'TOOL_CLAMP':
      if (a.clamp === s.toolClamped) return { s }
      if (!a.clamp && (s.spindleOn || isBusy(s)))
        return { s, msg: 'Разжим запрещён: шпиндель вращается или идёт цикл', level: 'error' }
      return {
        s: { ...s, toolClamped: a.clamp },
        msg: a.clamp ? 'Инструмент зажат' : 'Инструмент разжат',
        level: a.clamp ? 'info' : 'warn',
      }

    case 'LUBE':
      if (s.run === 'ESTOP') return { s, msg: 'Смазка недоступна при аварийном стопе', level: 'warn' }
      return { s: { ...s, lube: 3000 }, msg: 'Принудительная смазка направляющих' }

    case 'GO_SAFE': {
      if (s.run === 'ESTOP' || s.run === 'ALARM') return { s, msg: 'Сначала сбросьте аварию', level: 'error' }
      const blocked = blockedFromMotion(s, 'Остановите программу перед выходом в безопасную точку')
      if (blocked) return { s: blocked }
      return {
        s: {
          ...s,
          run: 'RUN',
          homing: true,
          workZero: false,
          motion: { target: { ...s.machine, Z: 0 }, rapid: true, feed: 0 },
        },
        msg: 'Выход в безопасную точку: Z0, затем X0 Y0',
      }
    }

    case 'MDI_BUFFER':
      return { s: { ...s, mdi: { ...s.mdi, buffer: a.value.toUpperCase() } } }

    case 'MDI_RECALL': {
      const text = s.mdi.history[a.index]
      return { s: text ? { ...s, mdi: { ...s.mdi, buffer: text } } : s }
    }
  }
}
