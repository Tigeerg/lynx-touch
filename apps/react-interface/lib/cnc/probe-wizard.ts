// Probe wizard subsystem: the probe screen state (the step of the measurement cycle, the
// tool call, writing the result into an offset and the retry). Extracted from machine.ts
// after the pattern of ladder.ts and prog.ts: the reducer composes the subsystems, and the
// size of the machine is held by the tools/check.ps1 mark. The measurement run itself
// (probeTick) lives in probe-exec.ts. A result is { s, msg, level }; machine.ts shows the
// message.
import { clamp } from './clamp'
import { WORKPLACE_LABEL } from './data'
import { isBusy } from './guards'
import { PROBE_STEP_MEASURE, PROBE_STEP_RESULT, PROBE_TOOL } from './probe'
import { MODE_LOCKED_SCREENS, type MachineState, type Message } from './types'

export type ProbeWizardAction =
  | { type: 'PROBE_OPEN'; open?: boolean }
  | { type: 'PROBE_STEP'; dir: 1 | -1 }
  | { type: 'PROBE_CALL_TOOL' }
  | { type: 'PROBE_WRITE' }
  | { type: 'PROBE_RETRY' }

const PROBE_TYPES = new Set<string>(['PROBE_OPEN', 'PROBE_STEP', 'PROBE_CALL_TOOL', 'PROBE_WRITE', 'PROBE_RETRY'])

export const isProbeWizardAction = (a: { type: string }): a is ProbeWizardAction => PROBE_TYPES.has(a.type)

export type Result = { s: MachineState; msg?: string; level?: Message['level'] }

export function probeWizardReducer(s: MachineState, a: ProbeWizardAction): Result {
  switch (a.type) {
    case 'PROBE_OPEN': {
      const open = a.open ?? !s.probe.open
      if (!open && s.probe.running) return { s, msg: 'Дождитесь окончания измерения или нажмите СБРОС', level: 'warn' }
      if (open && isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      if (open && MODE_LOCKED_SCREENS[s.mode].includes('offset'))
        return { s, msg: `Щуп недоступен в режиме «${WORKPLACE_LABEL[s.workplace]}»`, level: 'warn' }
      return {
        s: {
          ...s,
          screen: open ? 'offset' : s.screen,
          probe: { ...s.probe, open, step: open ? 0 : s.probe.step, result: open ? null : s.probe.result },
        },
      }
    }

    case 'PROBE_STEP': {
      if (s.probe.running) return { s }
      const step = clamp(s.probe.step + a.dir, 0, PROBE_STEP_MEASURE)
      if (a.dir > 0 && s.probe.step === 1 && s.tool !== PROBE_TOOL)
        return { s, msg: 'Установите щуп T99 в шпиндель (F3)', level: 'warn' }
      if (a.dir > 0 && s.probe.step === PROBE_STEP_MEASURE)
        return { s, msg: 'Нажмите ПУСК для измерения', level: 'warn' }
      const from = s.probe.step === PROBE_STEP_RESULT && a.dir < 0 ? PROBE_STEP_MEASURE : step
      return { s: { ...s, probe: { ...s.probe, step: from } } }
    }

    case 'PROBE_CALL_TOOL':
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      return {
        s: { ...s, tool: PROBE_TOOL, pendingTool: PROBE_TOOL, spindleOn: false },
        msg: 'Щуп T99 в шпинделе. Шпиндель заблокирован',
      }

    case 'PROBE_WRITE': {
      const r = s.probe.result
      if (!r) return { s }
      const i = s.cursor.offset
      const offsets = s.offsets.map((o, idx) => (idx === i ? { ...o, values: { ...o.values, ...r.values } } : o))
      const axes = Object.keys(r.values).join(' ')
      const prev = s.probe.prev ?? { ...s.offsets[i].values }
      return {
        s: { ...s, offsets, probe: { ...s.probe, written: true, prev } },
        msg: `Записано в ${s.offsets[i].code}: ${axes}`,
      }
    }

    case 'PROBE_RETRY':
      return {
        s: {
          ...s,
          probe: { ...s.probe, step: PROBE_STEP_MEASURE, result: null, written: false, prev: null, elapsed: 0 },
        },
      }
  }
}
