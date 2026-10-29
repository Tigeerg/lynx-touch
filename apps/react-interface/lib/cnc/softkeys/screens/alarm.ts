// The alarm screen: acknowledge, clear the acknowledged, reset.
import { Check, RotateCcw, Trash2 } from 'lucide-react'
import type { MachineState } from '../../types'
import { slots, type Softkey } from '../softkey-slots'

export const alarmScreen = (s: MachineState): (Softkey | null)[] =>
  slots([
    {
      label: 'Квитировать',
      icon: Check,
      action: { type: 'ALARM_ACK' },
      disabled: !s.alarms[s.cursor.alarm]?.active,
    },
    { label: 'Удалить квитированные', icon: Trash2, action: { type: 'ALARM_CLEAR_ALL' } },
    { label: 'Сброс', icon: RotateCcw, action: { type: 'RESET' }, disabled: s.run === 'ESTOP' },
    null,
    null,
    null,
    null,
    null,
  ])
