// The softkey bar, public surface. Each set lives in its own module under ./softkeys (windows,
// workplaces, screens) and is re-exported here, so the importers of './softkeys' keep one entry
// point while the modules stay small (CLAUDE.md, section 2). getSoftkeys picks the set in the
// order the panel does: a window, then the workplace, then the screen.
import type { MachineState } from './types'
import { calc, fileDialog, search, toolDialog, toolMeasure } from './softkeys/softkey-dialogs'
import { alarmScreen, offsetScreen, posScreen, progScreen, serviceScreen, toolScreen } from './softkeys/softkey-screens'
import type { Softkey } from './softkeys/softkey-slots'
import { runProg, setupPos, testWorkplace } from './softkeys/softkey-workplaces'

export { SERVICE_ICON } from './softkeys/softkey-icons'
export { SOFTKEY_LABEL_MAX, SOFTKEY_SLOTS, softkeyAction } from './softkeys/softkey-slots'
export type { Softkey } from './softkeys/softkey-slots'

export function getSoftkeys(s: MachineState): (Softkey | null)[] {
  const busy = s.run === 'RUN' || s.run === 'HOLD'

  if (s.fileDialog) return fileDialog()

  if (s.toolDialog) return toolDialog()

  if (s.calc) return calc()

  if (s.search.open) return search(s, busy)

  if (s.toolMeasure.open) return toolMeasure(s)

  // Screens with their own wizard (offsets, service, editor) keep their softkeys.
  if (s.workplace === 'SETUP' && s.screen === 'pos') return setupPos(s, busy)

  if (s.workplace === 'TEST' && !s.editMode) return testWorkplace(s, busy)

  if (s.workplace === 'RUN' && s.screen === 'prog' && !s.editMode) return runProg(s, busy)

  switch (s.screen) {
    case 'pos':
      return posScreen(s, busy)
    case 'prog':
      return progScreen(s, busy)
    case 'offset':
      return offsetScreen(s, busy)
    case 'tool':
      return toolScreen(s, busy)
    case 'service':
      return serviceScreen(s, busy)
    case 'alarm':
      return alarmScreen(s)
  }
}
