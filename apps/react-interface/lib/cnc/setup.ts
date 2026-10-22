// Setup subsystem: the offset and tool screens plus zeroing (work and relative done).
// Extracted from machine.ts after the pattern of ladder.ts and prog.ts: the reducer
// composes the subsystems, and the size of the machine is held by the tools/check.ps1
// mark. A result is { s, msg, level }; machine.ts shows the message.
import { zeroAxes } from './coords'
import { blockedFromMotion, isBusy } from './guards'
import { PROBE_TOOL } from './probe'
import { AXES, type Axis, type MachineState, type Message } from './types'

export type SetupAction =
  | { type: 'OFFSET_NEXT' }
  | { type: 'OFFSET_ACTIVATE' }
  | { type: 'OFFSET_TEACH'; axis: Axis }
  | { type: 'OFFSET_CLEAR' }
  | { type: 'ZERO_REL' }
  | { type: 'ZERO_WORK'; axis?: Axis }
  | { type: 'GO_WORK_ZERO' }
  | { type: 'TOOL_PROBE' }
  | { type: 'TOOL_DIALOG'; open?: boolean }
  | { type: 'TOOL_INPUT'; value: string }
  | { type: 'TOOL_BACK' }
  | { type: 'TOOL_RUN' }
  | { type: 'TOOL_CALL' }
  | { type: 'TOOL_MEASURE' }
  | { type: 'TOOL_RESET_LIFE' }

const SETUP_TYPES = new Set<string>([
  'OFFSET_NEXT',
  'OFFSET_ACTIVATE',
  'OFFSET_TEACH',
  'OFFSET_CLEAR',
  'ZERO_REL',
  'ZERO_WORK',
  'GO_WORK_ZERO',
  'TOOL_PROBE',
  'TOOL_DIALOG',
  'TOOL_INPUT',
  'TOOL_BACK',
  'TOOL_RUN',
  'TOOL_CALL',
  'TOOL_MEASURE',
  'TOOL_RESET_LIFE',
])

export const isSetupAction = (a: { type: string }): a is SetupAction => SETUP_TYPES.has(a.type)

export type Result = { s: MachineState; msg?: string; level?: Message['level'] }

export function setupReducer(s: MachineState, a: SetupAction): Result {
  switch (a.type) {
    case 'OFFSET_NEXT': {
      if (isBusy(s)) return { s, msg: 'Смена системы координат недоступна во время цикла', level: 'warn' }
      const i = (s.activeOffset + 1) % s.offsets.length
      return { s: { ...s, activeOffset: i }, msg: `Активная система: ${s.offsets[i].code}` }
    }

    case 'GO_WORK_ZERO': {
      if (s.run === 'ESTOP' || s.run === 'ALARM') return { s, msg: 'Сначала сбросьте аварию', level: 'error' }
      const blocked = blockedFromMotion(s, 'Остановите программу перед переходом к нулю детали')
      if (blocked) return { s: blocked }
      return {
        s: {
          ...s,
          run: 'RUN',
          workZero: true,
          homing: false,
          motion: { target: { ...s.machine, Z: 0 }, rapid: true, feed: 0 },
        },
        msg: `К нулю ${s.offsets[s.activeOffset].code}: подъём Z, затем X0 Y0`,
      }
    }

    case 'TOOL_PROBE': {
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      if (s.spindleOn) return { s, msg: 'Остановите шпиндель перед измерением', level: 'warn' }
      if (s.tool === PROBE_TOOL) return { s, msg: 'В шпинделе щуп T99', level: 'error' }
      const i = s.tools.findIndex((t) => t.number === s.tool)
      if (i < 0) return { s, msg: 'В шпинделе нет инструмента из таблицы', level: 'warn' }
      const length = Math.round((60 + ((Math.abs(s.machine.Z) * 7.31 + i * 13.7) % 70)) * 1000) / 1000
      const tools = s.tools.map((t, idx) => (idx === i ? { ...t, length } : t))
      const t = tools[i]
      return {
        s: { ...s, tools },
        msg: `Датчик: T${String(t.number).padStart(2, '0')} длина ${length.toFixed(3)} мм, Ø ${t.diameter.toFixed(3)} мм`,
      }
    }

    case 'TOOL_DIALOG': {
      const open = a.open ?? !s.toolDialog
      if (open && isBusy(s)) return { s, msg: 'Остановите программу, чтобы вызвать инструмент', level: 'warn' }
      return { s: { ...s, toolDialog: open, toolBuffer: open ? '' : s.toolBuffer } }
    }

    case 'TOOL_INPUT':
      return { s: { ...s, toolBuffer: (s.toolBuffer + a.value).replace(/^0+(?=\d)/, '').slice(0, 2) } }

    case 'TOOL_BACK':
      return { s: { ...s, toolBuffer: s.toolBuffer.slice(0, -1) } }

    case 'TOOL_RUN': {
      const n = s.toolBuffer ? Number(s.toolBuffer) : 0
      if (!n) return { s, msg: 'Введите номер инструмента', level: 'warn' }
      const i = s.tools.findIndex((t) => t.number === n)
      if (i < 0) return { s, msg: `Инструмент T${n} отсутствует в таблице`, level: 'error' }
      if (s.mode === 'AUTO' || isBusy(s)) return { s, msg: 'Смена инструмента в режиме РУЧНОЙ / MDI', level: 'warn' }
      const t = s.tools[i]
      return {
        s: {
          ...s,
          toolDialog: false,
          toolBuffer: '',
          cursor: { ...s.cursor, tool: i },
          tool: t.number,
          pendingTool: t.number,
          spindleOn: false,
        },
        msg: `T${String(t.number).padStart(2, '0')} M6: «${t.name}» в шпинделе`,
      }
    }

    case 'OFFSET_ACTIVATE': {
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      const offset = s.offsets[s.cursor.offset]
      if (!offset) return { s, msg: 'Смещение не выбрано', level: 'warn' }
      return { s: { ...s, activeOffset: s.cursor.offset }, msg: `Активно смещение ${offset.code}` }
    }

    case 'OFFSET_TEACH': {
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      const i = s.cursor.offset
      const offset = s.offsets[i]
      if (!offset) return { s, msg: 'Смещение не выбрано', level: 'warn' }
      const offsets = s.offsets.map((o, idx) =>
        idx === i ? { ...o, values: { ...o.values, [a.axis]: s.machine[a.axis] } } : o,
      )
      return { s: { ...s, offsets }, msg: `${offset.code} ${a.axis} = ${s.machine[a.axis].toFixed(3)}` }
    }

    case 'OFFSET_CLEAR': {
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      const i = s.cursor.offset
      const offset = s.offsets[i]
      if (!offset) return { s, msg: 'Смещение не выбрано', level: 'warn' }
      const offsets = s.offsets.map((o, idx) => (idx === i ? { ...o, values: zeroAxes() } : o))
      return { s: { ...s, offsets }, msg: `${offset.code} обнулено` }
    }

    case 'TOOL_CALL': {
      if (s.mode === 'AUTO' || isBusy(s)) return { s, msg: 'Смена инструмента в режиме РУЧНОЙ / MDI', level: 'warn' }
      const tool = s.tools[s.cursor.tool]
      if (!tool) return { s, msg: 'Инструмент не выбран', level: 'warn' }
      return {
        s: { ...s, tool: tool.number, pendingTool: tool.number },
        msg: `Инструмент T${String(tool.number).padStart(2, '0')} в шпинделе`,
      }
    }

    case 'TOOL_MEASURE': {
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      const i = s.cursor.tool
      const measured = s.tools[i]
      if (!measured) return { s, msg: 'Инструмент не выбран', level: 'warn' }
      const length = Math.round((60 + ((Math.abs(s.machine.Z) * 7.31 + i * 13.7) % 70)) * 1000) / 1000
      const tools = s.tools.map((t, idx) => (idx === i ? { ...t, length } : t))
      return {
        s: { ...s, tools },
        msg: `T${String(measured.number).padStart(2, '0')}: длина ${length.toFixed(3)} мм`,
      }
    }

    case 'TOOL_RESET_LIFE': {
      const i = s.cursor.tool
      const tools = s.tools.map((t, idx) => (idx === i ? { ...t, life: 100 } : t))
      const number = s.tools[i]?.number
      const toolTime = number === undefined ? s.toolTime : { ...s.toolTime, [number]: 0 }
      return { s: { ...s, tools, toolTime }, msg: 'Ресурс инструмента сброшен' }
    }

    case 'ZERO_REL':
      return { s: { ...s, relOrigin: { ...s.machine } }, msg: 'Относительные координаты обнулены' }

    case 'ZERO_WORK': {
      if (s.mode !== 'JOG') return { s, msg: 'Обнуление рабочих координат — в режиме РУЧНОЙ', level: 'warn' }
      if (isBusy(s)) return { s, msg: 'Остановите станок', level: 'warn' }
      const axes = a.axis ? [a.axis] : AXES
      const offsets = s.offsets.map((o, idx) => {
        if (idx !== s.activeOffset) return o
        const values = { ...o.values }
        for (const ax of axes) values[ax] = s.machine[ax]
        return { ...o, values }
      })
      const code = s.offsets[s.activeOffset].code
      return { s: { ...s, offsets }, msg: a.axis ? `${code}: ${a.axis} = 0` : `${code}: все оси обнулены` }
    }
  }
}
