// Operator messages and machine alarms: the single place that writes the message line and
// the alarm log. Extracted from machine.ts (tools/check.ps1 mark). The level sets the
// lifetime of the line: an error has no limit - it stays until the next message or a reset,
// so the operator has time to read it.
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
