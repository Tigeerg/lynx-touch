// Сообщения оператору и аварии станка: единая точка записи строки сообщений и
// журнала аварий. Вынесено из machine.ts (метка tools/check.ps1). Уровень задаёт
// время жизни строки: у ошибки оно не ограничено — держится до следующего сообщения
// или сброса, чтобы оператор успел прочитать.
import type { AlarmLevel, MachineState, Message } from './types'

const TTL_MS: Record<Message['level'], number> = { info: 3500, warn: 6000, error: 0 }

export const timeNow = () => new Date().toLocaleTimeString('ru-RU', { hour12: false })

export const notify = (s: MachineState, text: string, level: Message['level'] = 'info'): MachineState => ({
  ...s,
  message: { text, level, ttl: TTL_MS[level] },
})

export const pushAlarm = (s: MachineState, code: number, level: AlarmLevel, text: string): MachineState => ({
  ...s,
  alarms: [{ id: `a${s.alarmSeq}`, code, level, text, time: timeNow(), active: true }, ...s.alarms],
  alarmSeq: s.alarmSeq + 1,
})
