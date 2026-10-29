// The tool screen: tool change, length measure, life reset, clamp.
import { Lock, LockOpen, RefreshCcw, Ruler, Wrench } from 'lucide-react'
import type { MachineState } from '../../types'
import { slots, type Softkey } from '../softkey-slots'

export const toolScreen = (s: MachineState, busy: boolean): (Softkey | null)[] =>
  slots([
    {
      label: 'Вызвать в шпиндель',
      icon: Wrench,
      action: { type: 'TOOL_CALL' },
      disabled: s.mode === 'AUTO' || busy,
    },
    { label: 'Замер длины', icon: Ruler, action: { type: 'TOOL_MEASURE' }, disabled: busy },
    { label: 'Сброс ресурса', icon: RefreshCcw, action: { type: 'TOOL_RESET_LIFE' } },
    {
      label: s.toolClamped ? 'Отжать' : 'Зажать',
      icon: s.toolClamped ? LockOpen : Lock,
      action: { type: 'TOOL_CLAMP', clamp: !s.toolClamped },
      active: s.toolClamped,
      disabled: s.toolClamped && (s.spindleOn || busy),
    },
    null,
    null,
    null,
    null,
  ])
