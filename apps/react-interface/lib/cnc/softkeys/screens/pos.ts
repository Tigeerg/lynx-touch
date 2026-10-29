// The Position screen: probing, the base pick, moving to a known point and the calculator.
import { Calculator, Crosshair, House, LocateFixed, Lock, LockOpen, Repeat, Ruler, Wrench } from 'lucide-react'
import { SOFTKEY_LABEL } from '../../data'
import { PROBE_TOOL } from '../../probe'
import type { MachineState } from '../../types'
import { slots, type Softkey } from '../softkey-slots'

export const posScreen = (s: MachineState, busy: boolean): (Softkey | null)[] =>
  slots([
    {
      label: SOFTKEY_LABEL.TOOL_PROBE,
      icon: Ruler,
      action: { type: 'TOOL_PROBE' },
      disabled: busy || s.spindleOn || s.tool === PROBE_TOOL,
    },
    {
      label: SOFTKEY_LABEL.PROBE_OPEN,
      icon: LocateFixed,
      action: { type: 'PROBE_OPEN', open: true },
      disabled: busy,
    },
    {
      label: `СК ${s.offsets[s.activeOffset].code}`,
      icon: Repeat,
      action: { type: 'OFFSET_NEXT' },
      disabled: busy,
    },
    {
      label: SOFTKEY_LABEL.GO_SAFE,
      icon: House,
      action: { type: 'GO_SAFE' },
      disabled: s.run === 'ESTOP' || s.run === 'ALARM' || busy || s.machineLock,
    },
    {
      label: 'В ноль детали',
      icon: Crosshair,
      action: { type: 'GO_WORK_ZERO' },
      disabled: s.run === 'ESTOP' || s.run === 'ALARM' || busy || s.machineLock,
    },
    {
      label: 'Зажим оси A',
      icon: s.aBrake ? Lock : LockOpen,
      action: { type: 'TOGGLE', key: 'aBrake' },
      active: s.aBrake,
    },
    { label: 'Вызов инструмента', icon: Wrench, action: { type: 'TOOL_DIALOG', open: true }, disabled: busy },
    { label: 'Калькулятор режимов', icon: Calculator, action: { type: 'CALC', open: true }, active: s.calc },
  ])
