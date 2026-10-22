// Tool-length measurement subsystem (the tool setter): opening the modal, starting and
// aborting the run, and the phases the dialog animates. Extracted after the pattern of cycle.ts
// and prog.ts: the reducer composes the subsystems, and the size of the machine is held by the
// tools/check.ps1 mark. A result is { s, msg, level }; machine.ts shows the message.
import { isBusy } from './guards'
import type { Result } from './subsystem'
import type { MachineState, ToolMeasureState } from './types'

export type MeasurePhase = 'idle' | 'rapid' | 'fine' | 'touch' | 'retract' | 'done'

/** Seconds per phase of a run; the sum is the whole run. */
export const MEASURE_RAPID_S = 1.4
export const MEASURE_FINE_S = 1.8
export const MEASURE_TOUCH_S = 0.5
export const MEASURE_RETRACT_S = 1.1
export const MEASURE_TOTAL_S = MEASURE_RAPID_S + MEASURE_FINE_S + MEASURE_TOUCH_S + MEASURE_RETRACT_S

/** The tool setter's catalogue: feeds, travels and the breakage tolerance, shown by the dialog. */
export const MEASURE_PARAMS = {
  rapidFeed: 3000,
  fineFeed: 50,
  zTravel: 201,
  retract: 3,
  breakageTolerance: 2,
} as const

export const initialToolMeasure: ToolMeasureState = { open: false, running: false, elapsed: 0, length: null }

// The rapid approach takes most of the travel and the fine approach the rest, so the tool does
// not crash into the setter: the share is of the whole depth, not of the time.
const RAPID_SHARE = 0.78

const easeOut = (t: number) => 1 - (1 - t) ** 2
const easeInOut = (t: number) => (t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2)

export interface MeasureFrame {
  phase: MeasurePhase
  /** 0 - the spindle is up, 1 - the tool touches the setter. */
  depth: number
  triggered: boolean
  progress: number
}

export function measureFrame(m: ToolMeasureState): MeasureFrame {
  if (!m.running && m.length === null) return { phase: 'idle', depth: 0, triggered: false, progress: 0 }
  if (!m.running) return { phase: 'done', depth: 0, triggered: false, progress: 1 }
  const t = m.elapsed
  const progress = Math.min(1, t / MEASURE_TOTAL_S)
  if (t < MEASURE_RAPID_S) {
    return { phase: 'rapid', depth: easeOut(t / MEASURE_RAPID_S) * RAPID_SHARE, triggered: false, progress }
  }
  const fine = t - MEASURE_RAPID_S
  if (fine < MEASURE_FINE_S) {
    return {
      phase: 'fine',
      depth: RAPID_SHARE + (fine / MEASURE_FINE_S) * (1 - RAPID_SHARE),
      triggered: false,
      progress,
    }
  }
  const touch = fine - MEASURE_FINE_S
  if (touch < MEASURE_TOUCH_S) return { phase: 'touch', depth: 1, triggered: true, progress }
  const back = touch - MEASURE_TOUCH_S
  return { phase: 'retract', depth: 1 - easeInOut(Math.min(1, back / MEASURE_RETRACT_S)), triggered: false, progress }
}

/** The length the setter reads for the selected tool, mm: the simulated sensor. */
export function measuredLength(s: MachineState): number {
  const i = s.cursor.tool
  return Math.round((60 + ((Math.abs(s.machine.Z) * 7.31 + i * 13.7) % 70)) * 1000) / 1000
}

function startToolMeasure(s: MachineState): MachineState {
  if (s.toolMeasure.running) return s
  return { ...s, toolMeasure: { open: true, running: true, elapsed: 0, length: null } }
}

/** Advances the run on TICK; finishes by writing the measured length into the tool table. */
export function tickToolMeasure(s: MachineState, dt: number): { s: MachineState; msg?: string } {
  const m = s.toolMeasure
  if (!m.running) return { s }
  if (s.run === 'ESTOP' || s.run === 'ALARM') {
    return { s: { ...s, toolMeasure: { ...m, running: false, elapsed: 0 } }, msg: 'Замер прерван' }
  }
  const elapsed = m.elapsed + dt / 1000
  if (elapsed < MEASURE_TOTAL_S) return { s: { ...s, toolMeasure: { ...m, elapsed } } }
  const length = measuredLength(s)
  const i = s.cursor.tool
  const tools = s.tools.map((t, idx) => (idx === i ? { ...t, length } : t))
  return {
    s: { ...s, tools, toolMeasure: { ...m, running: false, elapsed: MEASURE_TOTAL_S, length } },
    msg: `T${String(tools[i].number).padStart(2, '0')}: длина ${length.toFixed(3)} мм`,
  }
}

export type ToolMeasureAction =
  { type: 'TOOL_MEASURE' } | { type: 'TOOL_MEASURE_START' } | { type: 'TOOL_MEASURE_CLOSE' }

const TOOL_MEASURE_TYPES = new Set<string>(['TOOL_MEASURE', 'TOOL_MEASURE_START', 'TOOL_MEASURE_CLOSE'])

export const isToolMeasureAction = (a: { type: string }): a is ToolMeasureAction => TOOL_MEASURE_TYPES.has(a.type)

export function toolMeasureReducer(s: MachineState, a: ToolMeasureAction): Result {
  switch (a.type) {
    case 'TOOL_MEASURE': {
      if (isBusy(s)) return { s, msg: 'Остановите программу перед замером', level: 'warn' }
      if (!s.tools[s.cursor.tool]) return { s, msg: 'Инструмент не выбран', level: 'warn' }
      return { s: { ...s, toolMeasure: { open: true, running: false, elapsed: 0, length: null } } }
    }

    case 'TOOL_MEASURE_START': {
      if (!s.toolMeasure.open) return { s }
      if (s.run === 'ESTOP' || s.run === 'ALARM') return { s, msg: 'Сначала сбросьте аварию', level: 'warn' }
      return { s: startToolMeasure(s) }
    }

    case 'TOOL_MEASURE_CLOSE':
      return { s: { ...s, toolMeasure: initialToolMeasure } }
  }
}
