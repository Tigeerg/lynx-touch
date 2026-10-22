// Length of the active screen list, for cursor navigation. Extracted from machine.ts so the
// reducer does not grow (tools/check.ps1 mark).
import { programLines } from './files'
import { ladderRungs } from './ladder'
import { serviceListLength } from './service-data'
import type { MachineState, Screen } from './types'

export function listLength(s: MachineState, screen: Screen) {
  if (screen === 'prog') return programLines(s).length
  if (screen === 'offset') return s.offsets.length
  if (screen === 'tool') return s.tools.length
  if (screen === 'alarm') return s.alarms.length
  if (screen === 'service')
    return s.service.section === 'ladder' ? ladderRungs(s).length : serviceListLength(s.service.section)
  return 1
}
