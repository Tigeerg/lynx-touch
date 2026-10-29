// The cycle subsystem reducer: the program run - the machine tick, start and hold, reset and
// emergency stop. The block execution it works through lives in cycle-exec.ts, the action union
// and its predicate in cycle-actions.ts; cycle.ts is the facade. The reducer composes the
// subsystems, and the size of the machine is held by the tools/check.ps1 mark. A result is
// { s, msg, level }; machine.ts shows the message.
import { WORKPLACE_LABEL } from '../data'
import { programLines } from '../files'
import { isBusy } from '../guards'
import { notify, pushAlarm } from '../messages'
import { PROBE_STEP_MEASURE, PROBE_TOOL } from '../probe'
import { abortProbe, probeTick } from '../probe-exec'
import { idleSim, tickSim } from '../prog'
import { evaluateLadder, sameLadderMem, sameLadderSignals } from '../ladder-scan'
import { isCutting } from '../spindle'
import { tickToolMeasure } from '../tool-measure'
import type { Dispatch, Result } from '../subsystem'
import { MDI_HISTORY_SIZE, type MachineState } from '../types'
import type { CycleAction } from './cycle-actions'
import { applyBlock, completeBlock, executeLine, stepMotion } from './cycle-exec'

export function cycleReducer(s: MachineState, a: CycleAction, dispatch: Dispatch): Result {
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
      if (n.toolMeasure.running) {
        const m = tickToolMeasure(n, a.dt)
        return { s: m.msg ? notify(m.s, m.msg) : m.s }
      }
      if (n.run !== 'RUN') return { s: n }
      if (n.probe.running) {
        const r = probeTick(n, a.dt)
        return { s: r.msg ? notify(r.s, r.msg) : r.s }
      }
      if (n.mode === 'AUTO') n = { ...n, cycleTime: n.cycleTime + a.dt }
      if (n.spindleOn && isCutting(n))
        n = { ...n, toolTime: { ...n.toolTime, [n.tool]: (n.toolTime[n.tool] ?? 0) + a.dt } }
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
      // The PROGON chooses what ПУСК runs (F4): the axis-free simulation, the dry run, the step
      // run or the handwheel run. The branch is before the AUTO one, because the workplace keeps
      // the AUTO mode.
      if (s.workplace === 'TEST') {
        const start = s.nextLine >= programLines(s).length ? 0 : s.nextLine
        if (s.test.runMode === 'dry') {
          return { s: executeLine({ ...s, sim: idleSim(), run: 'RUN', dryRun: true }, start) }
        }
        if (s.test.runMode === 'step') {
          // One block per ПУСК: the block stops the run when it is done, whatever the F5 toggle
          // says, so a stray single-block key cannot turn the step run into an uninterrupted one.
          const next = executeLine({ ...s, sim: idleSim(), run: 'RUN', dryRun: false }, start)
          return { s: { ...next, stopAfterBlock: true }, msg: `По шагам: кадр ${start + 1}. ПУСК — следующий кадр` }
        }
        if (s.test.runMode === 'mpg') {
          // The handwheel run has no feed of its own: it is armed here and the MPG advances the
          // toolpath block by block (mpg.ts needs the dry run, so it is switched on).
          return {
            s: { ...s, sim: idleSim(), run: 'IDLE', dryRun: true },
            msg: `Прогон маховиком: кадр ${s.line + 1}. Вращайте маховик`,
          }
        }
        // The SIM branch owns the guards (an empty program) and the message words; the PROGON
        // adds only one thing: no dry run is left on while the axes stand still.
        const next = dispatch(s, { type: 'SIM', on: true })
        return { s: next.sim.active ? { ...next, dryRun: false } : next }
      }
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
      // The PROGON simulation has no feed of its own: СТОП stops it, so the key is not dead.
      if (s.sim.active) return { s: { ...s, sim: idleSim() }, msg: 'Симуляция остановлена', level: 'warn' }
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
        a.at,
      )
      return { s: n, msg: 'АВАРИЙНЫЙ СТОП', level: 'error' }
    }
  }
}
