// Program screen subsystem: choosing the NC file and editing its blocks. Extracted
// from machine.ts after the pattern of ladder.ts and prog.ts: the reducer composes the
// subsystems, and the size of the machine is held by the tools/check.ps1 mark. A result
// is { s, msg, level }; machine.ts shows the message.
//
// Editing works on a block buffer (editor): Enter edits, Ins inserts, Del deletes;
// commit writes the buffer into the file, cancel drops it. The file write goes through
// files.ts (setLines); the file state lives in MachineState.
import { clampIndex } from './clamp'
import { SCREEN_LABEL, WORKPLACE_LABEL } from './data'
import { currentFile, programLines, setLines } from './files'
import { isBusy } from './guards'
import { idleSim } from './prog'
import type { Result } from './subsystem'
import { MODE_LOCKED_SCREENS, type MachineState } from './types'

export type EditorAction =
  | { type: 'FILE_DIALOG'; open?: boolean }
  | { type: 'FILE_CURSOR'; delta: number }
  | { type: 'FILE_OPEN' }
  | { type: 'EDIT_MODE'; on?: boolean }
  | { type: 'EDIT_BEGIN' }
  | { type: 'EDIT_INSERT' }
  | { type: 'EDIT_DELETE' }
  | { type: 'EDIT_BUFFER'; value: string }
  | { type: 'EDIT_COMMIT' }
  | { type: 'EDIT_CANCEL' }

const EDITOR_TYPES = new Set<string>([
  'FILE_DIALOG',
  'FILE_CURSOR',
  'FILE_OPEN',
  'EDIT_MODE',
  'EDIT_BEGIN',
  'EDIT_INSERT',
  'EDIT_DELETE',
  'EDIT_BUFFER',
  'EDIT_COMMIT',
  'EDIT_CANCEL',
])

export const isEditorAction = (a: { type: string }): a is EditorAction => EDITOR_TYPES.has(a.type)

export function editorReducer(s: MachineState, a: EditorAction): Result {
  switch (a.type) {
    case 'FILE_DIALOG': {
      const open = a.open ?? !s.fileDialog
      if (open && isBusy(s)) return { s, msg: 'Остановите программу, чтобы открыть другой файл', level: 'warn' }
      return { s: { ...s, fileDialog: open, fileCursor: s.activeFile, editor: open ? null : s.editor } }
    }

    case 'FILE_CURSOR':
      return { s: { ...s, fileCursor: clampIndex(s.fileCursor, a.delta, s.files.length) } }

    case 'FILE_OPEN': {
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      const f = s.files[s.fileCursor]
      return {
        s: {
          ...s,
          activeFile: s.fileCursor,
          fileDialog: false,
          screen: MODE_LOCKED_SCREENS[s.mode].includes('prog') ? s.screen : 'prog',
          editMode: false,
          editor: null,
          line: 0,
          nextLine: 0,
          cycleTime: 0,
          sim: idleSim(),
          cursor: { ...s.cursor, prog: 0 },
        },
        msg: `Открыт файл ${f.name} — ${f.title}`,
      }
    }

    case 'EDIT_MODE': {
      const on = a.on ?? !s.editMode
      if (on && isBusy(s)) return { s, msg: 'Редактирование доступно только в останове', level: 'warn' }
      if (on && MODE_LOCKED_SCREENS[s.mode].includes('prog'))
        return {
          s,
          msg: `В режиме «${WORKPLACE_LABEL[s.workplace]}» экран «${SCREEN_LABEL.prog}» заблокирован`,
          level: 'warn',
        }
      if (on)
        return {
          s: { ...s, editMode: true, screen: 'prog' },
          msg: 'Редактирование: Enter — изменить, Ins — вставить, Del — удалить',
        }
      return { s: { ...s, editMode: false, editor: null }, msg: `Файл ${currentFile(s).name} сохранён` }
    }

    case 'EDIT_BEGIN': {
      if (!s.editMode) return { s }
      const i = s.cursor.prog
      return { s: { ...s, editor: { line: i, buffer: programLines(s)[i], inserted: false } } }
    }

    case 'EDIT_INSERT': {
      if (!s.editMode) return { s }
      const i = s.cursor.prog + 1
      const lines = [...programLines(s)]
      lines.splice(i, 0, '')
      const n = setLines(s, lines)
      return { s: { ...n, cursor: { ...s.cursor, prog: i }, editor: { line: i, buffer: '', inserted: true } } }
    }

    case 'EDIT_DELETE': {
      if (!s.editMode) return { s }
      const lines = programLines(s)
      if (lines.length <= 1) return { s, msg: 'Нельзя удалить последний кадр', level: 'warn' }
      const i = s.cursor.prog
      const n = setLines(
        s,
        lines.filter((_, idx) => idx !== i),
      )
      return {
        s: { ...n, cursor: { ...s.cursor, prog: clampIndex(i, 0, lines.length - 1) }, line: 0, nextLine: 0 },
        msg: `Кадр ${i + 1} удалён`,
      }
    }

    case 'EDIT_BUFFER':
      return { s: s.editor ? { ...s, editor: { ...s.editor, buffer: a.value.toUpperCase() } } : s }

    case 'EDIT_COMMIT': {
      if (!s.editor) return { s }
      const value = s.editor.buffer.trim()
      if (!value) return editorReducer(s, { type: 'EDIT_CANCEL' })
      const line = s.editor.line
      const lines = programLines(s).map((l, idx) => (idx === line ? value : l))
      return { s: { ...setLines(s, lines), editor: null }, msg: `Кадр ${line + 1} изменён` }
    }

    case 'EDIT_CANCEL': {
      if (!s.editor) return { s }
      if (s.editor.inserted) {
        const i = s.editor.line
        const n = setLines(
          s,
          programLines(s).filter((_, idx) => idx !== i),
        )
        return { s: { ...n, editor: null, cursor: { ...s.cursor, prog: Math.max(0, i - 1) } } }
      }
      return { s: { ...s, editor: null } }
    }
  }
}
