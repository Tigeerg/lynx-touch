// «Открыть в редакторе» from the overlay of the quick program (PLAN-QUICKPROG, 5 step 11).
//
// After a save the header offers the program the save wrote. The predicate reads the document, so
// the button and the action share one rule; the action closes the overlay and opens the file the
// way the file list does (`FILE_OPEN` over `fileCursor`, `editor.ts`), not by a second write path.
import { editorReducer } from '../editor'
import { isBusy } from '../guards'
import type { Result } from '../subsystem'
import type { MachineState } from '../types'
import type { CamState } from './types'

/** The file «Открыть в редакторе» opens: the name the last save wrote, or null when there is none. */
export const openTarget = (cam: CamState): string | null =>
  cam.saved === null || cam.saved.trim() === '' ? null : cam.saved

/** The overlay closed and the saved program in the editor, or the reason the press does nothing. */
export function openSaved(s: MachineState, cam: CamState): Result {
  const name = openTarget(cam)
  if (name === null) return { s, msg: 'Сначала сохраните программу', level: 'warn' }
  if (isBusy(s)) return { s, msg: 'Остановите программу, чтобы открыть файл', level: 'warn' }
  const index = s.files.findIndex((file) => file.name === name)
  if (index < 0) return { s, msg: `Файл ${name} не найден в списке`, level: 'warn' }
  // The editor's own action, not a second `activeFile` write: it also rewinds the run and closes
  // a dialog. The overlay leaves afterwards, so the screen shows the program just written.
  const opened = editorReducer({ ...s, fileCursor: index }, { type: 'FILE_OPEN' })
  if (opened.s.activeFile !== index) return opened
  return { s: { ...opened.s, cam: null }, msg: `Открыт файл ${name}` }
}
