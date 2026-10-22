// Cycle subsystem: the program run - the machine tick, start and hold, reset and emergency
// stop - and the motion helpers it works through (executing a block, applying it, stepping
// the axes, finishing a block). Extracted from machine.ts after the pattern of ladder.ts and
// prog.ts: the reducer composes the subsystems, and the size of the machine is held by the
// tools/check.ps1 mark. executeLine is exported because the SPINDLE_WARMUP case (machine.ts)
// starts a program through it. A result is { s, msg, level }; machine.ts shows the message.
import { WORKPLACE_LABEL } from './data'
import { programLines } from './files'
import { isBusy } from './guards'
import { parseWords } from './gcode'
import { notify, pushAlarm } from './messages'
import { motionSpeed, toWork, workOffset, zeroAxes } from './coords'
import { PROBE_STEP_MEASURE, PROBE_TOOL } from './probe'
import { abortProbe, probeTick } from './probe-exec'
import { idleSim, tickSim } from './prog'
import { evaluateLadder, sameLadderMem, sameLadderSignals } from './ladder-scan'
import type { Result } from './subsystem'
import { AXES, MDI_HISTORY_SIZE, type Axes, type MachineState } from './types'

export function executeLine(state: MachineState, index: number): MachineState {
  const lines = programLines(state)
  // A negative index is a caller defect (a stale cursor): lines[-1] is undefined, and parseWords
  // threw on it, taking the whole panel down. End the program instead of crashing.
  if (index < 0 || index >= lines.length) return { ...state, programEnd: true }
  if (state.blockSkip && lines[index].trimStart().startsWith('/')) {
    return { ...state, line: index, nextLine: index + 1, dwell: 0 }
  }
  return applyBlock({ ...state, line: index, nextLine: index + 1 }, lines[index], index > 0)
}

export function applyBlock(state: MachineState, text: string, allowMotion: boolean): MachineState {
  let s: MachineState = { ...state, dwell: 150 }
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
      s.dwell = 1800
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

function stepMotion(s: MachineState, dt: number): MachineState {
  if (!s.motion) return s
  if (s.machineLock) return { ...s, motion: null }
  const { target } = s.motion
  const dist = Math.hypot(...AXES.map((ax) => target[ax] - s.machine[ax]))
  const travel = (motionSpeed(s) / 60000) * dt
  if (dist <= 0.0005 || travel >= dist) return { ...s, machine: { ...target }, motion: null }
  if (travel === 0) return s
  const k = travel / dist
  const machine = { ...s.machine }
  for (const ax of AXES) machine[ax] += (target[ax] - machine[ax]) * k
  return { ...s, machine }
}

function completeBlock(s: MachineState): MachineState {
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

export type CycleAction =
  | { type: 'TICK'; dt: number }
  | { type: 'CYCLE_START' }
  | { type: 'FEED_HOLD' }
  | { type: 'RESET' }
  | { type: 'ESTOP' }
  | { type: 'SPINDLE_WARMUP' }

const CYCLE_TYPES = new Set<string>(['TICK', 'CYCLE_START', 'FEED_HOLD', 'RESET', 'ESTOP', 'SPINDLE_WARMUP'])

export const isCycleAction = (a: { type: string }): a is CycleAction => CYCLE_TYPES.has(a.type)

export function cycleReducer(s: MachineState, a: CycleAction): Result {
  switch (a.type) {
    case 'TICK': {
      let n = s
      if (n.message && n.message.ttl > 0) {
        const ttl = n.message.ttl - a.dt
        n = { ...n, message: ttl > 0 ? { ...n.message, ttl } : null }
      }
      if (n.lube > 0) n = { ...n, lube: Math.max(0, n.lube - a.dt) }
      const scan = evaluateLadder(n, undefined, a.dt)
      if (!sameLadderMem(scan.mem, n.ladder.mem) || !sameLadderSignals(scan.signals, n.ladder.signals))
        n = { ...n, ladder: { ...n.ladder, mem: scan.mem, signals: scan.signals } }
      n = tickSim(n, a.dt)
      if (n.run !== 'RUN') return { s: n }
      if (n.probe.running) {
        const r = probeTick(n, a.dt)
        return { s: r.msg ? notify(r.s, r.msg) : r.s }
      }
      if (n.mode === 'AUTO') n = { ...n, cycleTime: n.cycleTime + a.dt }
      if (n.spindleOn) n = { ...n, toolTime: { ...n.toolTime, [n.tool]: (n.toolTime[n.tool] ?? 0) + a.dt } }
      if (n.dwell > 0) return { s: { ...n, dwell: Math.max(0, n.dwell - a.dt) } }
      if (n.motion) return { s: stepMotion(n, a.dt) }
      return { s: completeBlock(n) }
    }

    case 'CYCLE_START': {
      if (s.run === 'ESTOP') return { s, msg: 'Аварийный стоп активен — отпустите кнопку', level: 'error' }
      if (s.run === 'ALARM') return { s, msg: 'Есть авария — нажмите СБРОС', level: 'error' }
      if (s.run === 'RUN') return { s }
      if (s.run === 'HOLD') return { s: { ...s, sim: idleSim(), run: 'RUN' }, msg: 'Подача возобновлена' }
      if (s.probe.open && s.screen === 'offset') {
        if (s.probe.step !== PROBE_STEP_MEASURE)
          return { s, msg: 'ПУСК щупа доступен на шаге «Измерение»', level: 'warn' }
        if (s.tool !== PROBE_TOOL) return { s, msg: 'В шпинделе нет щупа T99', level: 'error' }
        if (s.spindleOn) return { s, msg: 'Остановите шпиндель перед измерением', level: 'error' }
        return {
          s: {
            ...s,
            sim: idleSim(),
            run: 'RUN',
            probe: {
              ...s.probe,
              running: true,
              elapsed: 0,
              origin: { ...s.machine },
              result: null,
              written: false,
              prev: null,
            },
          },
          msg: 'Измерение щупом… G38.2',
        }
      }
      if (s.editMode) return { s, msg: 'Выйдите из режима редактирования', level: 'warn' }
      if (s.mode === 'AUTO') {
        const start = s.nextLine >= programLines(s).length ? 0 : s.nextLine
        return { s: executeLine({ ...s, sim: idleSim(), run: 'RUN' }, start) }
      }
      if (s.workplace === 'SETUP') {
        const text = s.mdi.buffer.trim().toUpperCase()
        if (!text) return { s, msg: 'Введите кадр MDI', level: 'warn' }
        if (!s.toolClamped && /M0?[34]\b/.test(text))
          return { s, msg: 'Инструмент разжат — зажмите перед пуском шпинделя', level: 'error' }
        const history = [text, ...s.mdi.history.filter((h) => h !== text)].slice(0, MDI_HISTORY_SIZE)
        const n = applyBlock(
          { ...s, sim: idleSim(), run: 'RUN', mdi: { buffer: '', history, running: true } },
          text,
          true,
        )
        return { s: { ...n, programEnd: false, stopAfterBlock: false }, msg: `MDI: ${text}` }
      }
      return { s, msg: `ПУСК недоступен в режиме «${WORKPLACE_LABEL[s.workplace]}»`, level: 'warn' }
    }

    case 'FEED_HOLD':
      return s.run === 'RUN'
        ? { s: { ...s, run: 'HOLD' }, msg: 'Стоп подачи. ПУСК — продолжить', level: 'warn' }
        : { s }

    case 'RESET': {
      if (s.run === 'ESTOP') return { s, msg: 'Сначала отпустите аварийный стоп', level: 'error' }
      return {
        s: {
          ...abortProbe(s),
          run: 'IDLE',
          motion: null,
          dwell: 0,
          homing: false,
          workZero: false,
          sim: idleSim(),
          programEnd: false,
          stopAfterBlock: false,
          spindleOn: false,
          coolant: false,
          mist: false,
          mdi: { ...s.mdi, running: false },
          line: 0,
          nextLine: 0,
          cycleTime: 0,
          alarms: s.alarms.map((al) => (al.level === 'error' ? { ...al, active: false } : al)),
        },
        msg: 'Сброс выполнен',
      }
    }

    case 'SPINDLE_WARMUP': {
      if (s.run === 'ESTOP' || s.run === 'ALARM') return { s, msg: 'Сначала сбросьте аварию', level: 'error' }
      if (isBusy(s)) return { s, msg: 'Сначала остановите программу', level: 'warn' }
      if (!s.toolClamped) return { s, msg: 'Инструмент разжат — прогрев запрещён', level: 'error' }
      if (s.tool === PROBE_TOOL) return { s, msg: 'В шпинделе щуп T99 — прогрев запрещён', level: 'error' }
      const idx = s.files.findIndex((f) => /ПРОГРЕВ/i.test(f.title))
      if (idx < 0) return { s, msg: 'Программа прогрева шпинделя не найдена', level: 'error' }
      const base: MachineState = {
        ...s,
        activeFile: idx,
        workplace: 'RUN',
        mode: 'AUTO',
        dryRun: false,
        screen: 'prog',
        cursor: { ...s.cursor, prog: 0 },
      }
      return {
        s: executeLine({ ...base, sim: idleSim(), run: 'RUN', line: 0, nextLine: 0 }, 0),
        msg: 'Прогрев шпинделя запущен',
      }
    }

    case 'ESTOP': {
      if (s.run === 'ESTOP')
        return { s: { ...s, run: 'ALARM' }, msg: 'Аварийный стоп отпущен — нажмите СБРОС', level: 'warn' }
      const n = pushAlarm(
        {
          ...abortProbe(s),
          run: 'ESTOP',
          motion: null,
          dwell: 0,
          homing: false,
          workZero: false,
          sim: idleSim(),
          spindleOn: false,
          coolant: false,
          mist: false,
          mdi: { ...s.mdi, running: false },
        },
        1001,
        'error',
        'Нажата кнопка аварийного останова',
      )
      return { s: n, msg: 'АВАРИЙНЫЙ СТОП', level: 'error' }
    }
  }
}
