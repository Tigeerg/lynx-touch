// The spindle readout layer: rpm, "is cutting" and the load model of the side panels. Three
// screens used to repeat these formulas; now they read one module. The load is a demonstration
// model, not a sensor: it is a smooth function of the cycle time, kept here so every screen
// shows the same number.
import type { Axis, MachineState } from './types'

const PERCENT = 100

// Load model constants (percent of the spindle rating).
const LOAD_IDLE = 6
const LOAD_CUTTING_BASE = 38
const LOAD_WAVE_PRIMARY = 9
const LOAD_WAVE_SECONDARY = 5
const LOAD_SECONDARY_FACTOR = 2.7
const LOAD_CYCLE_DIVISOR = 400
const LOAD_MIN = 0
const LOAD_MAX = 100

// Axis load model (percent of the axis rating): the distance still to go, measured in mm on the
// work side; the load rests at a few percent while the axis stands still in a cut.
const AXIS_LOAD_REST = 4
const AXIS_LOAD_BASE = 20
const AXIS_LOAD_SPAN_MM = 5
const AXIS_LOAD_SPAN = 60
const AXIS_MOVED_MM = 0.0005

/** Spindle speed in rpm: the setpoint scaled by the spindle override; 0 when off or in ESTOP. */
export function spindleRpm(state: MachineState): number {
  if (!state.spindleOn || state.run === 'ESTOP') return 0
  return Math.round((state.spindleSet * state.spindleOverride) / PERCENT)
}

/** True when the machine is running a feed move (not a rapid). */
export function isCutting(state: MachineState): boolean {
  return state.run === 'RUN' && state.motion !== null && !state.motion.rapid
}

/**
 * Spindle load in percent, 0..100. A demonstration model, not a sensor reading: a constant base
 * for idle and cutting plus two slow sine waves of the cycle time, so the panel shows a lively
 * but deterministic number.
 */
export function spindleLoad(state: MachineState): number {
  if (spindleRpm(state) === 0) return LOAD_MIN
  if (!isCutting(state)) return LOAD_IDLE
  const t = state.cycleTime / LOAD_CYCLE_DIVISOR
  const load =
    LOAD_CUTTING_BASE + Math.sin(t) * LOAD_WAVE_PRIMARY + Math.sin(t * LOAD_SECONDARY_FACTOR) * LOAD_WAVE_SECONDARY
  return Math.min(LOAD_MAX, Math.max(LOAD_MIN, Math.round(load)))
}

/**
 * Axis load in percent, 0..100: 0 when the machine is not cutting, a small rest value when the
 * axis has arrived in a cut, and a share of the distance still to go (5 mm reaches 80 %). A
 * demonstration model, not a sensor reading, kept here so the side panels show one number.
 */
export function axisLoad(state: MachineState, axis: Axis): number {
  if (!isCutting(state) || !state.motion) return 0
  const distance = Math.abs(state.motion.target[axis] - state.machine[axis])
  if (distance <= AXIS_MOVED_MM) return AXIS_LOAD_REST
  return Math.min(LOAD_MAX, Math.round(AXIS_LOAD_BASE + (distance / AXIS_LOAD_SPAN_MM) * AXIS_LOAD_SPAN))
}
