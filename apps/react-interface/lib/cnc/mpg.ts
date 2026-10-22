// Handwheel run subsystem over the NC toolpath (MPG Feed / Dry Run). Extracted from
// machine.ts after the pattern of ladder.ts and prog.ts: the reducer composes the
// subsystems, and the size of the machine is held by the tools/check.ps1 mark. A result is
// { s, msg, level }; machine.ts shows the message.
//
// The run follows the "Dry run" modifier (dryRun): turning the handwheel moves the axes to
// the next toolpath block, without the spindle or coolant. Modal X/Y/Z/A and the work offset
// (G54-G59) are taken into account, so the position matches the NC program.
import { programLines } from './files'
import { parseWords } from './gcode'
import type { Result } from './subsystem'
import type { Axes, MachineState } from './types'

export type MpgAction = { type: 'MPG_HANDWHEEL'; dir: 1 | -1 }

const MPG_TYPES = new Set<string>(['MPG_HANDWHEEL'])

export const isMpgAction = (a: { type: string }): a is MpgAction => MPG_TYPES.has(a.type)

interface PathPoint {
  index: number
  machine: Axes
}

// Toolpath points: blocks with coordinates converted to machine ones, taking modal X/Y/Z/A
// and the work offset in force at the moment of the block into account.
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
