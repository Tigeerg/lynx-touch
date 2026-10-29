// The code of the offset under the cursor. The key must render even when the list is empty (the
// cursor reads an element that is not there), so an absent row shows a dash instead of taking
// the bar down.
import type { MachineState } from '../../types'

export const offsetCode = (s: MachineState) => s.offsets[s.cursor.offset]?.code ?? '—'
