// The measure phase as the operator reads it: the state line of the dialog. Kept beside the scene
// because the phase is the same word in both places.
import type { MeasurePhase } from '@/lib/cnc/tool-measure'

export const PHASE_LABEL: Record<MeasurePhase, string> = {
  idle: 'Готов к замеру',
  rapid: 'Быстрый подвод',
  fine: 'Медленный подвод',
  touch: 'Касание датчика',
  retract: 'Отвод шпинделя',
  done: 'Замер завершён',
}
