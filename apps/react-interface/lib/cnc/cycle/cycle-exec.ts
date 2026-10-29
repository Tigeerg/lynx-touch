// The block execution of the program run: executing a line, applying a block and its helpers
// (the motion step, the block completion). Extracted from cycle.ts after the pattern of
// ladder/: the reducer (cycle-reducer.ts) composes these, and cycle.ts is the facade. executeLine
// is exported because the SPINDLE_WARMUP case (machine.ts) starts a program through it. A result
// is { s, msg, level }; machine.ts shows the message.
import { MS_PER_MINUTE } from '../data'
import { programLines } from '../files'
import { parseWords } from '../gcode'
import { notify, pushAlarm } from '../messages'
import { motionSpeed, toWork, workOffset, zeroAxes } from '../coords'
import { AXES, type Axes, type MachineState } from '../types'

/** The dwell after a block with motion: how long the axes rest before the next block. */
const BLOCK_DWELL_MS = 150
/** The dwell of a tool change (M06): the real machine waits for the clamp. */
const TOOL_CHANGE_DWELL_MS = 1800

export function executeLine(state: MachineState, index: number): MachineState {
  const lines = programLines(state)
  // A negative index is a caller defect (a stale cursor): lines[-1] is undefined, and parseWords
  // threw on it, taking the whole panel down. Stop in ALARM instead of pretending M30 ran.
  if (index < 0) {
    const stopped = pushAlarm(
      { ...state, run: 'ALARM', programEnd: false, motion: null, dwell: 0 },
      9001,
      'error',
      'Внутренняя ошибка: курсор программы ушёл в отрицательный индекс',
      undefined,
    )
    return notify(stopped, 'Останов: сбой курсора программы', 'error')
  }
  if (index >= lines.length) return { ...state, programEnd: true }
  if (state.blockSkip && lines[index].trimStart().startsWith('/')) {
    return { ...state, line: index, nextLine: index + 1, dwell: 0 }
  }
  return applyBlock({ ...state, line: index, nextLine: index + 1 }, lines[index], index > 0)
}

export function applyBlock(state: MachineState, text: string, allowMotion: boolean): MachineState {
  let s: MachineState = { ...state, dwell: BLOCK_DWELL_MS }
  const coords: Partial<Axes> = {}
  let home = false
  const mCodes: number[] = []

  for (const { letter, value } of parseWords(text)) {
    if (letter === 'G') {
      if (value === 0 || value === 1) s.motionModal = value
      if (value >= 54 && value <= 59) s.activeOffset = value - 54
      if (value === 28) home = true
    } else if (letter === 'X' || letter === 'Y' || letter === 'Z' || letter === 'A') {
      coords[letter] = value
    } else if (letter === 'F') s.programFeed = value
    else if (letter === 'S') s.spindleSet = value
    else if (letter === 'T') s.pendingTool = value
    else if (letter === 'M') mCodes.push(value)
  }

  if (home) {
    s.motion = { target: { ...s.machine, Z: 0 }, rapid: true, feed: 0 }
  } else if (Object.keys(coords).length > 0 && allowMotion) {
    const off = workOffset(s)
    const work = toWork(s)
    const target = { ...zeroAxes() }
    for (const ax of AXES) target[ax] = (coords[ax] ?? work[ax]) + off[ax]
    s.motion = { target, rapid: s.motionModal === 0, feed: s.programFeed }
  }

  for (const m of mCodes) {
    if (m === 3 || m === 4) {
      s.spindleOn = true
      s.spindleDir = m === 3 ? 1 : -1
    }
    if (m === 5) s.spindleOn = false
    if (m === 6) {
      s.tool = s.pendingTool
      s.dwell = TOOL_CHANGE_DWELL_MS
    }
    if (m === 8) s.coolant = true
    if (m === 9) s.coolant = false
    if (m === 0 || (m === 1 && s.optStop)) {
      s.stopAfterBlock = true
      s = notify(s, m === 0 ? 'Программный останов M00' : 'Опциональный останов M01 — нажмите ПУСК')
    }
    if (m === 30) s.programEnd = true
  }

  if (s.singleBlock) s.stopAfterBlock = true
  return s
}

export function stepMotion(s: MachineState, dt: number): MachineState {
  if (!s.motion) return s
  if (s.machineLock) return { ...s, motion: null }
  const { target } = s.motion
  const dist = Math.hypot(...AXES.map((ax) => target[ax] - s.machine[ax]))
  const travel = (motionSpeed(s) / MS_PER_MINUTE) * dt
  if (dist <= 0.0005 || travel >= dist) return { ...s, machine: { ...target }, motion: null }
  if (travel === 0) return s
  const k = travel / dist
  const machine = { ...s.machine }
  for (const ax of AXES) machine[ax] += (target[ax] - machine[ax]) * k
  return { ...s, machine }
}

export function completeBlock(s: MachineState): MachineState {
  if (s.homing) {
    if (s.machine.X !== 0 || s.machine.Y !== 0) {
      return { ...s, motion: { target: { ...s.machine, X: 0, Y: 0 }, rapid: true, feed: 0 } }
    }
    return notify({ ...s, run: 'IDLE', homing: false }, 'Безопасная точка достигнута')
  }
  if (s.mdi.running) {
    return { ...s, run: 'IDLE', mdi: { ...s.mdi, running: false }, stopAfterBlock: false, programEnd: false }
  }
  if (s.workZero) {
    const off = workOffset(s)
    if (Math.abs(s.machine.X - off.X) > 0.0005 || Math.abs(s.machine.Y - off.Y) > 0.0005) {
      return { ...s, motion: { target: { ...s.machine, X: off.X, Y: off.Y, Z: 0 }, rapid: true, feed: 0 } }
    }
    return notify({ ...s, run: 'IDLE', workZero: false }, 'В нуле детали')
  }
  if (s.programEnd) {
    return notify(
      {
        ...s,
        run: 'IDLE',
        programEnd: false,
        line: 0,
        nextLine: 0,
        partCount: s.partCount + 1,
        lastCycle: s.cycleTime,
        cycleTime: 0,
        spindleOn: false,
        coolant: false,
      },
      'Программа завершена (M30). Деталь учтена',
    )
  }
  if (s.stopAfterBlock) return { ...s, run: 'IDLE', stopAfterBlock: false }
  return executeLine(s, s.nextLine)
}
