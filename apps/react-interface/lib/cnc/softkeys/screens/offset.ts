// The offsets screen: the probe wizard while it is open, the base table otherwise.
import {
  Check,
  ChevronLeft,
  ChevronRight,
  Crosshair,
  Eraser,
  LocateFixed,
  Play,
  RotateCcw,
  Save,
  X,
} from 'lucide-react'
import { SOFTKEY_LABEL } from '../../data'
import { PROBE_STEP_MEASURE, PROBE_STEP_RESULT, PROBE_TOOL } from '../../probe'
import type { MachineState } from '../../types'
import { offsetCode } from './offset-code'
import { slots, type Softkey } from '../softkey-slots'

export const offsetScreen = (s: MachineState, busy: boolean): (Softkey | null)[] => {
  if (s.probe.open) {
    const p = s.probe
    const atResult = p.step === PROBE_STEP_RESULT
    return slots([
      {
        label: 'Назад',
        icon: ChevronLeft,
        action: { type: 'PROBE_STEP', dir: -1 },
        disabled: p.running || p.step === 0,
      },
      {
        label: 'Далее',
        icon: ChevronRight,
        action: { type: 'PROBE_STEP', dir: 1 },
        disabled: p.running || p.step >= PROBE_STEP_MEASURE || (p.step === 1 && s.tool !== PROBE_TOOL),
      },
      {
        label: 'Щуп T99',
        icon: LocateFixed,
        action: { type: 'PROBE_CALL_TOOL' },
        active: s.tool === PROBE_TOOL,
        disabled: busy,
      },
      {
        label: `Записать в ${offsetCode(s)}`,
        icon: Save,
        action: { type: 'PROBE_WRITE' },
        active: p.written,
        disabled: !atResult || !p.result,
      },
      { label: 'Повторить', icon: RotateCcw, action: { type: 'PROBE_RETRY' }, disabled: !atResult },
      {
        label: 'Измерить',
        icon: Play,
        action: { type: 'CYCLE_START' },
        active: p.running,
        disabled:
          p.running ||
          p.step !== PROBE_STEP_MEASURE ||
          s.tool !== PROBE_TOOL ||
          s.spindleOn ||
          s.run === 'ESTOP' ||
          s.run === 'ALARM',
      },
      {
        label: 'Закрыть щуп',
        icon: X,
        action: { type: 'PROBE_OPEN', open: false },
        active: true,
        disabled: p.running,
      },
    ])
  }
  return slots([
    { label: 'Активировать', icon: Check, action: { type: 'OFFSET_ACTIVATE' }, disabled: busy },
    { label: 'Привязка X', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'X' }, disabled: busy },
    { label: 'Привязка Y', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'Y' }, disabled: busy },
    { label: 'Привязка Z', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'Z' }, disabled: busy },
    { label: 'Привязка A', icon: Crosshair, action: { type: 'OFFSET_TEACH', axis: 'A' }, disabled: busy },
    {
      label: `Обнулить ${offsetCode(s)}`,
      icon: Eraser,
      action: { type: 'OFFSET_CLEAR' },
      disabled: busy,
    },
    {
      label: SOFTKEY_LABEL.PROBE_OPEN,
      icon: LocateFixed,
      action: { type: 'PROBE_OPEN', open: true },
      disabled: busy,
    },
    {
      label: 'Обнулить по позиции',
      icon: Crosshair,
      action: { type: 'ZERO_WORK' },
      disabled: s.mode !== 'JOG' || busy,
    },
  ])
}
