// Подсистема прогона маховиком по траектории УП (MPG Feed / Dry Run). Вынесена из
// machine.ts по образцу ladder.ts и prog.ts: редуктор сводит подсистемы, а размер
// машины держит метка tools/check.ps1. Возвращаем { s, msg, level }, сообщение
// показывает machine.ts.
//
// Прогон идёт по модификатору «Холостой ход» (dryRun): поворот маховика переводит
// оси на следующий кадр траектории, без пуска шпинделя и СОЖ. Модальные X/Y/Z/A и
// рабочее смещение (G54-G59) учитываются, чтобы позиция совпадала с УП.
import { parseWords } from './gcode'
import { programLines } from './machine'
import type { Axes, MachineState, Message } from './types'

export type MpgAction = { type: 'MPG_HANDWHEEL'; dir: 1 | -1 }

const MPG_TYPES = new Set<string>(['MPG_HANDWHEEL'])

export const isMpgAction = (a: { type: string }): a is MpgAction => MPG_TYPES.has(a.type)

export type Result = { s: MachineState; msg?: string; level?: Message['level'] }

interface PathPoint {
  index: number
  machine: Axes
}

// Точки траектории: кадры с координатами, переведённые в машинные с учётом
// модальных X/Y/Z/A и действующего рабочего смещения на момент кадра.
function pathPoints(s: MachineState): PathPoint[] {
  const offset: Axes = { ...s.offsets[0].values }
  const work: Axes = { X: 0, Y: 0, Z: 0, A: 0 }
  const points: PathPoint[] = []
  programLines(s).forEach((text, index) => {
    let hasCoord = false
    for (const { letter, value } of parseWords(text)) {
      if (letter === 'G' && value >= 54 && value <= 59) Object.assign(offset, s.offsets[value - 54].values)
      else if (letter === 'X' || letter === 'Y' || letter === 'Z' || letter === 'A') {
        work[letter] = value
        hasCoord = true
      }
    }
    if (hasCoord) {
      points.push({
        index,
        machine: { X: work.X + offset.X, Y: work.Y + offset.Y, Z: work.Z + offset.Z, A: work.A + offset.A },
      })
    }
  })
  return points
}

export function mpgReducer(s: MachineState, a: MpgAction): Result {
  if (s.mode !== 'AUTO') return { s, msg: 'Прогон маховиком — в режиме АВТО', level: 'warn' }
  if (!s.dryRun) return { s, msg: 'Включите холостой ход — по нему идёт прогон маховиком', level: 'warn' }
  if (s.run === 'ESTOP' || s.run === 'ALARM') return { s, msg: 'Сначала сбросьте аварию', level: 'warn' }
  if (s.run === 'RUN' || s.run === 'HOLD')
    return { s, msg: 'Остановите программу перед прогоном маховиком', level: 'warn' }
  if (s.motion) return { s, msg: 'Дождитесь останова подачи', level: 'warn' }

  const points = pathPoints(s)
  if (points.length === 0) return { s, msg: 'В программе нет кадров с перемещением', level: 'warn' }

  const target = a.dir > 0 ? points.find((p) => p.index > s.line) : [...points].reverse().find((p) => p.index < s.line)
  if (!target) {
    return { s, msg: a.dir > 0 ? 'Траектория пройдена — конец УП' : 'Начало траектории', level: 'warn' }
  }

  return {
    s: { ...s, machine: { ...target.machine }, line: target.index, nextLine: target.index + 1, dwell: 0 },
    msg: `Прогон маховиком: кадр ${target.index + 1}`,
  }
}
