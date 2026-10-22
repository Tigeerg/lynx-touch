// Program screen subsystem: the run simulation without moving the axes and the text editing
// (search/replace, renumbering). Extracted from the machine after the pattern of ladder.ts:
// machine.ts composes the subsystems instead of absorbing them - the reducer size is held by
// the tools/check.ps1 mark. A result is { s, msg, level }; machine.ts shows the message.
import { programLines, setLines } from './files'
import { isBusy } from './guards'
import { pushAlarm } from './messages'
import { PROBE_TOOL } from './probe'
import { BROKEN_TOL_MM, sensorLength } from './tool-check'
import type { Result } from './subsystem'
import type { MachineState } from './types'

const SIM_STEP_MS = 120

const escapeRegExp = (t: string): string => t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')

export const idleSim = () => ({ active: false, line: 0, timer: 0 })

export type ProgAction =
  | { type: 'SIM'; on?: boolean }
  | { type: 'SEARCH'; open?: boolean }
  | { type: 'SEARCH_QUERY'; value: string }
  | { type: 'SEARCH_REPLACE'; value: string }
  | { type: 'SEARCH_FIND' }
  | { type: 'SEARCH_REPLACE_ALL'; at?: number }
  | { type: 'PROG_RENUMBER'; at?: number }
  | { type: 'TOOL_LIFE' }
  | { type: 'PART_COUNTER_RESET' }
  | { type: 'BROKEN_TOOL_CHECK'; at?: number }
  | { type: 'HINT'; text: string }

const PROG_TYPES = new Set<string>([
  'SIM',
  'SEARCH',
  'SEARCH_QUERY',
  'SEARCH_REPLACE',
  'SEARCH_FIND',
  'SEARCH_REPLACE_ALL',
  'PROG_RENUMBER',
  'TOOL_LIFE',
  'PART_COUNTER_RESET',
  'BROKEN_TOOL_CHECK',
  'HINT',
])

export const isProgAction = (a: { type: string }): a is ProgAction => PROG_TYPES.has(a.type)

// Simulation step on TICK: the blocks advance on a timer while the machine is not running a program.
export function tickSim(s: MachineState, dt: number): MachineState {
  if (!s.sim.active || s.run === 'RUN') return s
  const total = programLines(s).length
  let timer = s.sim.timer + dt
  let line = s.sim.line
  while (timer >= SIM_STEP_MS && line < total - 1) {
    line += 1
    timer -= SIM_STEP_MS
  }
  return { ...s, sim: { ...s.sim, line, timer } }
}

export function progReducer(s: MachineState, a: ProgAction): Result {
  switch (a.type) {
    case 'SIM': {
      const on = a.on ?? !s.sim.active
      if (on && isBusy(s)) return { s, msg: 'Остановите программу перед симуляцией', level: 'warn' }
      if (on && (s.run === 'ESTOP' || s.run === 'ALARM')) return { s, msg: 'Сначала сбросьте аварию', level: 'warn' }
      if (on && programLines(s).length < 2) return { s, msg: 'В программе нет кадров для прогона', level: 'warn' }
      return {
        s: { ...s, sim: on ? { active: true, line: s.cursor.prog, timer: 0 } : idleSim() },
        msg: on ? `Симуляция: прогон без движения осей, кадр ${s.cursor.prog + 1} — старт` : 'Симуляция остановлена',
      }
    }

    case 'SEARCH':
      return { s: { ...s, search: { ...s.search, open: a.open ?? !s.search.open } } }

    case 'SEARCH_QUERY':
      return { s: { ...s, search: { ...s.search, query: a.value } } }

    case 'SEARCH_REPLACE':
      return { s: { ...s, search: { ...s.search, replace: a.value } } }

    case 'SEARCH_FIND': {
      const q = s.search.query.trim().toUpperCase()
      if (!q) return { s, msg: 'Введите, что искать: номер кадра, T5, M06, G…', level: 'warn' }
      const lines = programLines(s)
      for (let k = 1; k <= lines.length; k++) {
        const i = (s.cursor.prog + k) % lines.length
        if (lines[i].toUpperCase().includes(q)) return { s: { ...s, cursor: { ...s.cursor, prog: i } } }
      }
      return { s, msg: `Не найдено: ${s.search.query.trim()}`, level: 'error' }
    }

    case 'SEARCH_REPLACE_ALL': {
      const q = s.search.query.trim()
      if (!q) return { s, msg: 'Введите, что заменить', level: 'warn' }
      if (isBusy(s)) return { s, msg: 'Замена недоступна во время выполнения', level: 'warn' }
      const re = new RegExp(escapeRegExp(q), 'gi')
      const replacement = s.search.replace
      let touched = 0
      const lines = programLines(s).map((l) => {
        if (!re.test(l)) return l
        re.lastIndex = 0
        touched += 1
        return l.replace(re, () => replacement)
      })
      if (touched === 0) return { s, msg: `Не найдено: ${q}`, level: 'error' }
      return { s: setLines(s, lines, a.at), msg: `Заменено в кадрах: ${touched}` }
    }

    case 'PROG_RENUMBER': {
      if (isBusy(s)) return { s, msg: 'Перенумерация недоступна во время выполнения', level: 'warn' }
      const lines = programLines(s)
      let changed = 0
      const nextLines = lines.map((l, i) => {
        const want = `N${(i + 1) * 10}`
        const lead = l.length - l.trimStart().length
        const indent = l.slice(0, lead)
        const body = l.slice(lead)
        if (!body.trim()) return l
        const out = /^N\s*\d+/i.test(body) ? `${indent}${body.replace(/^N\s*\d+/i, want)}` : `${indent}${want} ${body}`
        if (out !== l) changed += 1
        return out
      })
      if (changed === 0) return { s, msg: 'Нумерация уже по порядку' }
      return { s: setLines(s, nextLines, a.at), msg: `Перенумеровано кадров: ${changed}` }
    }

    case 'TOOL_LIFE': {
      const t = s.tools.find((x) => x.number === s.tool)
      return {
        s,
        msg: t
          ? `Ресурс инструмента T${String(t.number).padStart(2, '0')}: ${t.life}%`
          : 'Инструмент не найден в таблице',
      }
    }

    case 'PART_COUNTER_RESET': {
      if (isBusy(s)) return { s, msg: 'Обнулите счётчики в останове', level: 'warn' }
      return { s: { ...s, partCount: 0, cycleTime: 0, lastCycle: 0 }, msg: 'Счётчик деталей и таймер цикла обнулены' }
    }

    case 'BROKEN_TOOL_CHECK': {
      if (s.run === 'ESTOP' || s.run === 'ALARM') return { s, msg: 'Сначала сбросьте аварию', level: 'warn' }
      if (isBusy(s)) return { s, msg: 'Остановите программу перед проверкой', level: 'warn' }
      if (!s.toolClamped) return { s, msg: 'Инструмент разжат — проверка невозможна', level: 'error' }
      if (s.spindleOn) return { s, msg: 'Остановите шпиндель перед проверкой', level: 'warn' }
      if (s.tool === PROBE_TOOL) return { s, msg: 'В шпинделе щуп T99 — проверять нечего', level: 'warn' }
      const i = s.tools.findIndex((t) => t.number === s.tool)
      if (i < 0) return { s, msg: 'Инструмент не найден в таблице', level: 'warn' }

      const tool = s.tools[i]
      const num = `T${String(tool.number).padStart(2, '0')}`
      const measured = sensorLength(s, i)
      const delta = Math.round((measured - tool.length) * 1000) / 1000
      if (tool.life <= 0 || Math.abs(delta) > BROKEN_TOL_MM) {
        const alarmed = pushAlarm(
          { ...s, run: 'ALARM' },
          4010,
          'error',
          `Возможен слом ${num}: отклонение длины ${delta.toFixed(3)} мм`,
          a.at,
        )
        return {
          s: alarmed,
          msg: `Слом ${num}: отклонение ${delta.toFixed(3)} мм — проверьте инструмент`,
          level: 'error',
        }
      }
      return { s, msg: `${num}: длина ${measured.toFixed(3)} мм, отклонение ${delta.toFixed(3)} мм — в норме` }
    }

    case 'HINT':
      return { s, msg: a.text, level: 'warn' }
  }
}
